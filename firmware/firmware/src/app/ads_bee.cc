#include "ads_bee.hh"

#include <stdio.h> // for printing
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
// #include "hardware/dma.h"
#include "hardware/irq.h"
#include "capture.pio.h"
#include "pico/binary_info.h"
#include "hal.hh"

// #include <charconv> 
#include <string.h> // for strcat


const uint16_t kDecodeLEDBootupNumBlinks = 4;
const uint16_t kDecodeLEDBootupBlinkPeriodMs = 200;
const uint32_t kDecodeLEDOnMs = 100;
const float kPreambleDetectorFreq = 16e6; // Running at 16MHz (8 clock cycles per half bit).

ADSBee * isr_access = NULL;

void on_decode_complete() {
    isr_access->OnDecodeComplete();
}

ADSBee::ADSBee(ADSBeeConfig config_in) {
    config_ = config_in;

    preamble_detector_sm_ = pio_claim_unused_sm(config_.preamble_detector_pio, true);
    preamble_detector_offset_ = pio_add_program(config_.preamble_detector_pio, &preamble_detector_program);

    message_decoder_sm_ = pio_claim_unused_sm(config_.message_decoder_pio, true);
    message_decoder_offset_ = pio_add_program(config_.message_decoder_pio, &message_decoder_program);

    // Put IRQ parameters into the global scope for the on_decode_complete ISR.
    isr_access = this;

    // Figure out slice and channel values that will be used for setting PWM duty cycle.
    mtl_lo_pwm_slice_ = pwm_gpio_to_slice_num(config_.mtl_lo_pwm_pin);
    mtl_hi_pwm_slice_ = pwm_gpio_to_slice_num(config_.mtl_hi_pwm_pin);
    mtl_lo_pwm_chan_ = pwm_gpio_to_channel(config_.mtl_lo_pwm_pin);
    mtl_hi_pwm_chan_ = pwm_gpio_to_channel(config_.mtl_hi_pwm_pin);
}

void ADSBee::Init() {
    // Initialize the MTL bias PWM output.
    gpio_set_function(config_.mtl_lo_pwm_pin, GPIO_FUNC_PWM);
    gpio_set_function(config_.mtl_hi_pwm_pin, GPIO_FUNC_PWM);
    pwm_set_wrap(mtl_lo_pwm_slice_, kMTLMaxPWMCount);
    pwm_set_wrap(mtl_hi_pwm_slice_, kMTLMaxPWMCount); // redundant since it's the same slice

    SetMTLLoMilliVolts(kMTLLoDefaultMV);
    SetMTLHiMilliVolts(kMTLHiDefaultMV);
    pwm_set_enabled(mtl_lo_pwm_slice_, true);
    pwm_set_enabled(mtl_hi_pwm_slice_, true); // redundant since it's the same slice

    // Initialize the ML bias ADC input.
    adc_init();
    adc_gpio_init(config_.mtl_lo_adc_pin);
    adc_gpio_init(config_.mtl_hi_adc_pin);
    adc_gpio_init(config_.rssi_hold_adc_pin);

    // Initialize RSSI peak detector clear pin.
    gpio_init(config_.rssi_clear_pin);
    gpio_set_dir(config_.rssi_clear_pin, GPIO_OUT);
    gpio_put(config_.rssi_clear_pin, 1); // RSSI clear is active LO.

    // Calculate the PIO clock divider.
    float preamble_detector_div = (float)clock_get_hz(clk_sys) / kPreambleDetectorFreq;

    // Initialize the program using the .pio file helper function
    preamble_detector_program_init(
        config_.preamble_detector_pio, preamble_detector_sm_, preamble_detector_offset_, config_.pulses_pin,
        config_.decode_out_pin, preamble_detector_div
    );
    
    // enable the DECODE interrupt on PIO0_IRQ_0
    uint preamble_detector_decode_irq = PIO0_IRQ_0;
    pio_set_irq0_source_enabled(config_.preamble_detector_pio, pis_interrupt0, true); // state machine 0 IRQ 0

    /** MESSAGE DECODER PIO **/
    float message_decoder_freq = 16e6; // Run at 32 MHz to decode bits at 1Mbps.
    float message_decoder_div = (float)clock_get_hz(clk_sys) / message_decoder_freq;
    message_decoder_program_init(
        config_.message_decoder_pio, message_decoder_sm_, message_decoder_offset_,
        config_.pulses_pin, config_.recovered_clk_pin, message_decoder_div
    );

    printf("ADSBee::Init: PIOs initialized.\r\n");

    gpio_init(config_.status_led_pin);
    gpio_set_dir(config_.status_led_pin, GPIO_OUT);

    // FIXME: this doesn't work yet. Can an std::function be used as a (void *)?
    // irq_set_exclusive_handler(
    //     preamble_detector_decode_irq, 
    //     std::bind(
    //         &ADSBee::OnDecodeComplete, 
    //         this
    //     )
    // );
    irq_set_exclusive_handler(preamble_detector_decode_irq, on_decode_complete);
    irq_set_enabled(preamble_detector_decode_irq, true);

    // Enable the state machines
    pio_sm_set_enabled(config_.preamble_detector_pio, preamble_detector_sm_, true);
    pio_sm_set_enabled(config_.message_decoder_pio, message_decoder_sm_, true);

    // Blink the LED a few times to indicate a successful startup.
    for (uint16_t i = 0; i < kDecodeLEDBootupNumBlinks; i++) {
        gpio_put(config_.status_led_pin, 1);
        sleep_ms(kDecodeLEDBootupBlinkPeriodMs/2);
        gpio_put(config_.status_led_pin, 0);
        sleep_ms(kDecodeLEDBootupBlinkPeriodMs/2);
    }

}

void ADSBee::Update() {
    // Turn off the decode LED if it's been on for long enough.
    if (get_time_since_boot_ms() > led_off_timestamp_ms_) {
        gpio_put(config_.status_led_pin, 0);
    }

    // Update PWM output duty cycle.
    pwm_set_chan_level(mtl_lo_pwm_slice_, mtl_lo_pwm_chan_, mtl_lo_pwm_count_);
    pwm_set_chan_level(mtl_hi_pwm_slice_, mtl_hi_pwm_chan_, mtl_hi_pwm_count_);
}

void ADSBee::OnDecodeComplete() {
    if (at_config_mode_ != ATConfigMode_t::RUN) {
        pio_interrupt_clear(config_.preamble_detector_pio, 0);
        return; // don't ingest packets when in configuration mode.
    }

    // Read the RSSI level of the last packet.
    adc_select_input(config_.rssi_hold_adc_input);
    rssi_adc_counts_ = adc_read();
    gpio_put(config_.rssi_clear_pin, 0); // Clear RSSI peak detector.
    // Put RSSI clear HI later, to give peak detector some time to drain.

    uint16_t word_index = 0;
    while (!pio_sm_is_rx_fifo_empty(config_.message_decoder_pio, message_decoder_sm_)) {
        uint32_t word = pio_sm_get(config_.message_decoder_pio, message_decoder_sm_);
        printf("\t%d: %08x\r\n", word_index, word);

        switch(word_index) {
            case 0: {
                // Flush the previous word into a packet before beginning to store the new one.

                // First word out of the FIFO is actually the last word of the previously decoded message.
                // Assemble a packet buffer using the items in rx_buffer_.
                uint32_t packet_buffer[ADSBPacket::kMaxPacketLenWords32];
                packet_buffer[0] = rx_buffer_[0];
                packet_buffer[1] = rx_buffer_[1];
                packet_buffer[2] = rx_buffer_[2];
                // Trim extra ingested bit off of last word, then left-align.
                packet_buffer[3] = (static_cast<uint16_t>(word >> 1)) << 16;

                printf("New message: 0x%08x%08x%08x%04x RSSI=%d\r\n",
                    packet_buffer[0],
                    packet_buffer[1],
                    packet_buffer[2],
                    packet_buffer[3],
                    rssi_adc_counts_);
                ADSBPacket packet = ADSBPacket(packet_buffer, ADSBPacket::kMaxPacketLenWords32);
                if (packet.IsValid()) {
                    gpio_put(config_.status_led_pin, 1);
                    led_off_timestamp_ms_ = get_time_since_boot_ms() + kDecodeLEDOnMs;
                    printf("df=%d ca=%d tc=%d icao_address=0x%06x\r\n", 
                        packet.GetDownlinkFormat(),
                        packet.GetCapability(),
                        packet.GetTypeCode(),
                        packet.GetICAOAddress()
                    );
                }
                // printf(packet.IsValid() ? "\tVALID\r\n": "\tINVALID\r\n");
                break;
            }
            case 1:
            case 2:
            case 3:
                rx_buffer_[word_index-1] = word;
                break;
            // case 3:
            //     // Last word ingests an extra bit by accident, pinch it off here.
            //     rx_buffer_[word_index-1] = word>>1;

                break;
            default:
                // Received too many bits for this to be a valid packet. Throw away extra bits!
                printf("tossing");
                pio_sm_get(config_.message_decoder_pio, message_decoder_sm_); // throw away extra bits
        }
        word_index++;
    }

    gpio_put(config_.rssi_clear_pin, 1); // restore RSSI peak detector to working order.
    pio_interrupt_clear(config_.preamble_detector_pio, 0);
}

/**
 * @brief Set the high Minimum Trigger Level (MTL) at the AD8314 output in milliVolts.
 * @param[in] mtl_hi_mv_ Voltage level for a "high" trigger on the V_DN AD8314 output. The AD8314 has a nominal output
 * voltage of 2.25V on V_DN, with a logarithmic slope of around -42.6mV/dB and a minimum output voltage of 0.05V. Thus, power
 * levels received at the input of the AD8314 correspond to the following voltages.
 *      2250mV = -49dBm
 *      50mV = -2.6dBm
 * Note that there is a +30dB LNA gain stage in front of the AD8314, so for the overall receiver, the MTL values are
 * more like:
 *      2250mV = -79dBm
 *      50mV = -32.6dBm
 * @retval True if succeeded, False if MTL value was out of range.
*/
bool ADSBee::SetMTLHiMilliVolts(int mtl_hi_mv_) {
    if (mtl_hi_mv_ > kMTLMaxMV || mtl_hi_mv_ < kMTLMinMV) {
        printf("ADSBee::SetMTLHiMilliVolts: Unable to set mtl_hi_mv_ to %d, outside of permissible range %d-%d.\r\n", 
            mtl_hi_mv_, kMTLMinMV, kMTLMaxMV
        );
        return false;
    }
    mtl_hi_mv_ = mtl_hi_mv_;
    mtl_hi_pwm_count_ = mtl_hi_mv_ * kMTLMaxPWMCount / kVDDMV;

    return true;
}

/**
 * @brief Set the low Minimum Trigger Level (MTL) at the AD8314 output in milliVolts.
 * @param[in] mtl_hi_mv_ Voltage level for a "low" trigger on the V_DN AD8314 output. The AD8314 has a nominal output
 * voltage of 2.25V on V_DN, with a logarithmic slope of around -42.6mV/dB and a minimum output voltage of 0.05V. Thus, power
 * levels received at the input of the AD8314 correspond to the following voltages.
 *      2250mV = -49dBm
 *      50mV = -2.6dBm
 * Note that there is a +30dB LNA gain stage in front of the AD8314, so for the overall receiver, the MTL values are
 * more like:
 *      2250mV = -79dBm
 *      50mV = -32.6dBm
 * @retval True if succeeded, False if MTL value was out of range.
*/
bool ADSBee::SetMTLLoMilliVolts(int mtl_lo_mv_) {
    if (mtl_lo_mv_ > kMTLMaxMV || mtl_lo_mv_ < kMTLMinMV) {
        printf("ADSBee::SetMTLLoMilliVolts: Unable to set mtl_lo_mv_ to %d, outside of permissible range %d-%d.\r\n", 
            mtl_lo_mv_, kMTLMinMV, kMTLMaxMV
        );
        return false;
    }
    mtl_lo_mv_ = mtl_lo_mv_;
    mtl_lo_pwm_count_ = mtl_lo_mv_ * kMTLMaxPWMCount / kVDDMV;

    return true;
}

/**
 * @brief Return the value of the high Minimum Trigger Level threshold in milliVolts.
 * @retval MTL in milliVolts.
*/
int ADSBee::GetMTLHiMilliVolts() {
    return mtl_hi_mv_;
}

/**
 * @brief Return the value of the low Minimum Trigger Level threshold in milliVolts.
 * @retval MTL in milliVolts.
*/
int ADSBee::GetMTLLoMilliVolts() {
    return mtl_lo_mv_;
}

inline int adc_counts_to_mv(uint16_t adc_counts) {
    return 3300 * adc_counts / 0xFFF;
}

int ADSBee::ReadMTLHiMilliVolts() {
   // Read back the high level MTL bias output voltage.
    adc_select_input(config_.mtl_hi_adc_input);
    mtl_hi_adc_counts_ = adc_read();
    return adc_counts_to_mv(mtl_hi_adc_counts_);
}

int ADSBee::ReadMTLLoMilliVolts() {
    // Read back the low level MTL bias output voltage.
    adc_select_input(config_.mtl_lo_adc_input);
    mtl_lo_adc_counts_ = adc_read();
    return adc_counts_to_mv(mtl_lo_adc_counts_);
}

/**
//  * @brief Set the Minimum Trigger Level (MTL) at the AD8314 output in dBm.
//  * @param[in] mtl_threshold_dBm Power trigger level for a "high" trigger on the AD8314 output, in dBm.
//  * @retval True if succeeded, False if MTL value was out of range.
// */
// bool ADSBee::SetMTLdBm(int mtl_threshold_dBm) {

//     // BGA2818 is +30dBm
//     return true;
// }

/**
 * AT Commands
*/

