#include "MlatTime.h"
#include "SysTickTimeSource.h"
#include "ads_bee.hh"
#include "adsb_packet.hh"
#include "comms.hh"
#include "eeprom.hh"
#include "esp32_flasher.hh"
#include "hal.hh"
#include "pico/binary_info.h"
#include "settings.hh"
#include "unit_conversions.hh"

const char* kSoftwareVersionStr = "0.0.1";

ADSBee::ADSBeeConfig ads_bee_config;
// Override default config params here.
ADSBee ads_bee = ADSBee(ads_bee_config);
CommsManager comms_manager = CommsManager({});
ESP32SerialFlasher esp32_flasher = ESP32SerialFlasher({});
EEPROM eeprom = EEPROM({});
SettingsManager settings_manager;

int main() {
    bi_decl(bi_program_description("ADS-Bee ADSB Receiver"));

    // test code
    auto timeSource = adsbee::time::SysTimeTimeSourceFactory::create(true);
    auto timer = adsbee::time::MlatTime(timeSource);
    // end test code

    ads_bee.Init();
    eeprom.Init();
    comms_manager.Init();
    comms_manager.iface_printf(SettingsManager::SerialInterface::kConsole, "ADSBee 1090\r\nSoftware Version %s\r\n",
                               kSoftwareVersionStr);
    comms_manager.iface_printf(SettingsManager::SerialInterface::kCommsUART, "ADSBee 1090\r\nSoftware Version %s\r\n",
                               kSoftwareVersionStr);
    comms_manager.iface_printf(SettingsManager::SerialInterface::kGNSSUART, "ADSBee 1090\r\nSoftware Version %s\r\n",
                               kSoftwareVersionStr);

    settings_manager.Load();

    // Add a test aircraft to start.
    // TODO: Remove this.
    Aircraft test_aircraft;
    test_aircraft.airframe_type = Aircraft::AirframeType::kAirframeTypeSpaceTransatmosphericVehicle;
    strcpy(test_aircraft.callsign, "TST1234");
    test_aircraft.latitude_deg = 20;
    test_aircraft.longitude_deg = -140;
    test_aircraft.baro_altitude_ft = 10000;
    test_aircraft.vertical_rate_fpm = -5;
    test_aircraft.altitude_source = Aircraft::AltitudeSource::kAltitudeSourceBaro;
    test_aircraft.heading_deg = 100;
    test_aircraft.velocity_kts = 200;
    ads_bee.aircraft_dictionary.InsertAircraft(test_aircraft);

    while (true) {
        // Loop forever.
        comms_manager.Update();
        ads_bee.Update();

        TransponderPacket packet;
        while (ads_bee.transponder_packet_queue.Pop(packet)) {
            uint32_t packet_buffer[TransponderPacket::kMaxPacketLenWords32];
            packet.DumpPacketBuffer(packet_buffer);
            if (packet.GetPacketBufferLenBits() == TransponderPacket::kExtendedSquitterPacketLenBits) {
                CONSOLE_INFO("New message: 0x%08x|%08x|%08x|%04x RSSI=%ddBm MLAT=%u", packet_buffer[0],
                             packet_buffer[1], packet_buffer[2], (packet_buffer[3]) >> (4 * kBitsPerNibble),
                             packet.GetRSSIdBm(), packet.GetMLAT12MHzCounter());
            } else {
                CONSOLE_INFO("New message: 0x%08x|%06x RSSI=%ddBm MLAT=%u", packet_buffer[0],
                             (packet_buffer[1]) >> (2 * kBitsPerNibble), packet.GetRSSIdBm(),
                             packet.GetMLAT12MHzCounter());
            }

            if (packet.IsValid()) {
                // 112-bit (extended squitter) packets. These packets can be validated via CRC.
                ads_bee.FlashStatusLED();

                CONSOLE_INFO("\tdf=%d icao_address=0x%06x", packet.GetDownlinkFormat(), packet.GetICAOAddress());
                comms_manager.transponder_packet_reporting_queue.Push(packet);

                ads_bee.aircraft_dictionary.IngestADSBPacket(ADSBPacket(packet));
                CONSOLE_INFO("\taircraft_dictionary: %d aircraft", ads_bee.aircraft_dictionary.GetNumAircraft());
            } else if (packet.GetPacketBufferLenBits() == TransponderPacket::kSquitterPacketNumBits) {
                // CRC is overlaid with ICAO address for 56-bit (squitter) packets. Check ICAO against aircraft in the
                // dictionary to validate the CRC.
                if (ads_bee.aircraft_dictionary.ContainsAircraft(packet.GetICAOAddress())) {
                    ads_bee.FlashStatusLED();
                    CONSOLE_INFO("\tdf=%d icao_address=0x%06x", packet.GetDownlinkFormat(), packet.GetICAOAddress());
                    comms_manager.transponder_packet_reporting_queue.Push(packet);
                    // TODO: Add squitter packet support to aircraft dictionary.
                }
            }
        }
    }
}