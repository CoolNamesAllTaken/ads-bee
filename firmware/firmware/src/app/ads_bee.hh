#ifndef _ADS_BEE_HH_
#define _ADS_BEE_HH_

#include "ads_b_packet.hh"
#include "cpp_at.hh"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "stdint.h"

class ADSBee {
   public:
    struct ADSBeeConfig {
        PIO preamble_detector_pio = pio0;
        PIO message_decoder_pio = pio1;

        uint16_t status_led_pin = 15;
        uint16_t pulses_pin = 19;  // Reading ADS-B on GPIO22. Will look for DECODE signal on GPIO22-1 = GPIO21.
        uint16_t decode_in_pin = pulses_pin + 1;
        uint16_t decode_out_pin = 21;  // Use GPIO20 as DECODE signal output, will be wired to GPIO21 as input.
        uint16_t recovered_clk_pin =
            22;  // Use GPIO22 for the decode PIO program to output its recovered clock (for debugging only).
        // GPIO 24-25 used as PWM outputs for setting analog comparator threshold voltages.
        uint16_t mtl_lo_pwm_pin = 24;
        uint16_t mtl_hi_pwm_pin = 25;
        // GPIO 26-27 used as ADC inputs for reading analog comparator threshold voltages after RF filer.
        uint16_t mtl_lo_adc_pin = 26;
        uint16_t mtl_lo_adc_input = 0;
        uint16_t mtl_hi_adc_pin = 27;
        uint16_t mtl_hi_adc_input = 1;
        // GPIO 28 used as ADC input for the power level of the last decoded packet.
        uint16_t rssi_hold_adc_pin = 28;
        uint16_t rssi_hold_adc_input = 2;
        // GPIO 8 is used for clearing the RSSI peak detector.
        uint16_t rssi_clear_pin = 8;
        // GPIO 2-3 are used for the EEPROM and rx gain digipot I2C bus via I2C1.
        i2c_inst_t *onboard_i2c = i2c1;
        uint16_t onboard_i2c_sda_pin = 2;
        uint16_t onboard_i2c_scl_pin = 3;
        uint32_t onboard_i2c_clk_freq_hz = 400e3;  // 400kHz

        uint16_t esp32_enable_pin = 14;
        uint16_t esp32_gpio0_boot_pin = 11;

        uint16_t uart_tx_pin = 4;
        uint16_t uart_rx_pin = 5;
    };

    ADSBee(ADSBeeConfig config_in);
    bool Init();

    bool Update();
    void OnDecodeComplete();

    /**
     * Set the high Minimum Trigger Level (MTL) at the AD8314 output in milliVolts.
     * @param[in] mtl_hi_mv_ Voltage level for a "high" trigger on the V_DN AD8314 output. The AD8314 has a nominal
     * output voltage of 2.25V on V_DN, with a logarithmic slope of around -42.6mV/dB and a minimum output voltage of
     * 0.05V. Thus, power levels received at the input of the AD8314 correspond to the following voltages. 2250mV =
     * -49dBm 50mV = -2.6dBm Note that there is a +30dB LNA gain stage in front of the AD8314, so for the overall
     * receiver, the MTL values are more like: 2250mV = -79dBm 50mV = -32.6dBm
     * @retval True if succeeded, False if MTL value was out of range.
     */
    bool SetMTLHiMilliVolts(int mtl_hi_mv_);

    /**
     * Set the low Minimum Trigger Level (MTL) at the AD8314 output in milliVolts.
     * @param[in] mtl_hi_mv_ Voltage level for a "low" trigger on the V_DN AD8314 output. The AD8314 has a nominal
     * output voltage of 2.25V on V_DN, with a logarithmic slope of around -42.6mV/dB and a minimum output voltage of
     * 0.05V. Thus, power levels received at the input of the AD8314 correspond to the following voltages. 2250mV =
     * -49dBm 50mV = -2.6dBm Note that there is a +30dB LNA gain stage in front of the AD8314, so for the overall
     * receiver, the MTL values are more like: 2250mV = -79dBm 50mV = -32.6dBm
     * @retval True if succeeded, False if MTL value was out of range.
     */
    bool SetMTLLoMilliVolts(int mtl_lo_mv_);

    /**
     * Return the value of the high Minimum Trigger Level threshold in milliVolts.
     * @retval MTL in milliVolts.
     */
    int GetMTLHiMilliVolts() { return mtl_hi_mv_; }

    /**
     * Return the value of the low Minimum Trigger Level threshold in milliVolts.
     * @retval MTL in milliVolts.
     */
    int GetMTLLoMilliVolts() { return mtl_lo_mv_; }

    /**
     * Read the high Minimum Trigger Level threshold via ADC.
     * @retval MTL in milliVolts.
     */
    int ReadMTLHiMilliVolts();

    /**
     * Read the low Minimum Trigger Level threshold via ADC.
     * @retval MTL in milliVolts.
     */
    int ReadMTLLoMilliVolts();

    /**
     * Set the value of the receive signal path gain digipot over I2C and store the value for reference.
     * @param[in] rx_gain Gain (integer ratio between 1-101).
     * @retval True if setting was successful, false otherwise.
     */
    bool SetRxGain(int rx_gain);

    /**
     * Returns the last written value of rx_gain.
     * @retval Gain (integer ratio between 1-101).
     */
    int GetRxGain(int rx_gain) {
        return 1 + (rx_gain_digipot_resistance_ohms_ / 1e3);  // Non-inverting amp with R1 = 1kOhms.
    }

    /**
     * Reads the wiper value from the receive signal path gain digipot over I2C and calculates then returns the
     * resulting gain value.
     * @retval Gain ratio (integer between 1-101).
     */
    int ReadRxGain();

    const uint16_t kMTLMaxPWMCount = 5000;  // Clock is 125MHz, shoot for 25kHz PWM.
    const int kVDDMV = 3300;                // [mV] Voltage of positive supply rail.
    const int kMTLMaxMV = 3300;             // [mV]
    const int kMTLMinMV = 0;                // [mV]
    const int kMTLHiDefaultMV = 2000;       // [mV]
    const int kMTLLoDefaultMV = 3000;       // [mV]

    // Coefficients for calibrated polynomial equation to go from target MTL bias voltage to MTL bias PWM count.
    // const float kMTLBiasPWMCompCoeffX3 = 4.87299E-08;
    // const float kMTLBiasPWMCompCoeffX2 = -0.000376253;
    // const float kMTLBiasPWMCompCoeffX = 2.496361699;
    // const float kMTLBiasPWMCompCoeffConst = -16.0676799;

   private:
    ADSBeeConfig config_;
    CppAT parser_;

    uint32_t preamble_detector_sm_ = 0;
    uint32_t preamble_detector_offset_ = 0;

    uint32_t message_decoder_sm_ = 0;
    uint32_t message_decoder_offset_ = 0;

    uint32_t led_off_timestamp_ms_ = 0;

    uint16_t mtl_lo_pwm_slice_ = 0;
    uint16_t mtl_hi_pwm_slice_ = 0;
    uint16_t mtl_hi_pwm_chan_ = 0;
    uint16_t mtl_lo_pwm_chan_ = 0;

    uint16_t mtl_hi_mv_ = kMTLHiDefaultMV;
    uint16_t mtl_lo_mv_ = kMTLLoDefaultMV;
    uint16_t mtl_hi_pwm_count_ = 0;  // out of kMTLMaxPWMCount
    uint16_t mtl_lo_pwm_count_ = 0;  // out of kMTLMaxPWMCount

    uint16_t mtl_lo_adc_counts_ = 0;
    uint16_t mtl_hi_adc_counts_ = 0;
    uint16_t rssi_adc_counts_ = 0;

    uint32_t rx_gain_digipot_resistance_ohms_ = 100e3;

    // Due to a quirk, rx_buffer_ is used to store every word except for the first one.
    uint32_t rx_buffer_[ADSBPacket::kMaxPacketLenWords32 - 1];
};

#endif /* _ADS_BEE_HH_ */