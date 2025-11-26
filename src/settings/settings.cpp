/* Self header */
#include "settings.h"

/* Project headers */
#include "log/log.h"

/* Arduino headers */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FatFS.h>
#include <StreamUtils.h>
#include <m24c64.h>

/* Peripherals */
static m24c64 m_eeprom;

/* Json document */
StaticJsonDocument<CONFIG_SETTINGS_JSON_DOCUMENT_SIZE> m_doc;

/* Internal variables */
static bool m_loaded;                  //!< Flag indicating that settings have been loaded
static bool m_modified;                //!< Flag indicating that settings have been modified
static bool m_modified_immediate;      //!< Flag indicating that settings should be saved immediately
static uint32_t m_modified_timestamp;  //!< Timestamp of last modification used for deferred saving
static bool m_eeprom_available;        //!< Flag indicating that eeprom is available
static bool m_flash_available;         //!< Flag indicating that flash is available
static enum {
    FLASH_ACCESSOR_NONE,
    FLASH_ACCESSOR_LOCAL,
} m_flash_accessor;  //!< Enum used to track of who has exclusive access to flash
static struct {
    size_t start;    //!< Start address of the eeprom copy
    size_t size;     //!< Size of the eeprom copy
} m_eeprom_copy[2];  //!< Eeprom copies A and B

/**
 * @brief Computes the CRC-8 of a given data buffer.
 *
 * This function implements the CRC-8 algorithm as used by CDMA2000.
 * The parameters used are:
 *   - Polynomial: 0x9B (x^8 + x^7 + x^4 + x^3 + x + 1)
 *   - Initial value: 0xFF
 *   - Input and output are not reflected
 *   - No final XOR (XorOut = 0x00)
 *
 * @param[in] data   Pointer to the input data buffer.
 * @param[in] length Number of bytes in the buffer to process.
 * @return Calculated 8-bit CRC value.
 *
 * @see http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html#ch43 for implementation code reference
 * @see https://users.ece.cmu.edu/~koopman/crc/crc8.html for performance of the chosen polynomial
 * @see http://www.sunshine2k.de/coding/javascript/crc/crc_js.html for testing
 */
static uint8_t m_crc8(const void *const data, const size_t length) {
    const uint8_t polynomial = 0x9B;
    uint8_t crc = 0xFF;

    /* Process each byte in the data buffer */
    for (size_t i = 0; i < length; i++) {
        crc ^= ((uint8_t *)data)[i];

        /* Process each bit in the current byte */
        for (uint8_t j = 0; j < 8; j++) {
            if ((crc & 0x80) != 0) {
                crc = (uint8_t)((crc << 1) ^ polynomial);
            } else {
                crc <<= 1;
            }
        }
    }

    /* Return calculated CRC */
    return crc;
}

/**
 * @brief Computes the CRC-32 of a given data buffer.
 *
 * This function implements the CRC-32Q algorithm, which uses the following parameters:
 *   - Polynomial: 0x814141AB (x^32 + x^31 + x^28 + x^27 + x^25 + x^24 + x^23 + ...)
 *   - Initial value: Application provides @p initial (commonly 0x00000000)
 *   - Reflection: No input or output reflection (RefIn = false, RefOut = false)
 *   - Final XOR value: 0x00000000 (no final XOR)
 *
 * @param[in] initial Initial CRC value (commonly 0x00000000 for a new calculation).
 * @param[in] data    Pointer to the input byte buffer to process.
 * @param[in] length  Number of bytes to process from @p data.
 * @return            The computed CRC-32Q value.
 *
 * @see http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html#ch6 for implementation code reference
 * @see https://users.ece.cmu.edu/~koopman/crc/crc32.html for performance of the chosen polynomial
 * @see http://www.sunshine2k.de/coding/javascript/crc/crc_js.html for testing
 */
static uint32_t m_crc32(const uint32_t initial, const uint8_t *const data, const size_t length) {
    const uint32_t polynomial = 0x814141AB;
    uint32_t crc = initial;

    /* Process each byte in the data buffer */
    for (size_t i = 0; i < length; i++) {
        /* Bring the next byte into the top-most 8 bits of crc */
        crc ^= ((uint32_t)((uint8_t *)data)[i]) << 24;

        /* Process each bit of the current byte */
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x80000000) != 0) {
                crc = (uint32_t)((crc << 1) ^ polynomial);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Struct that acts as a custom reader for eeprom with CRC-32 tracking.
 *
 * This struct provides read methods compatible with ArduinoJson's custom Reader interface,
 * enabling automatic calculation of a running CRC-32 value over all data read from eeprom.
 *
 * @see https://arduinojson.org/v6/api/json/deserializejson/#custom-reader
 */
static struct {
    size_t index = 0;
    uint32_t crc = 0;

    /** @brief Reads one byte
     * @return the byte read, or -1 on failure */
    int read(void) {
        uint8_t c;
        int res = m_eeprom.read(index, &c, 1);
        if (res < 0) {
            log_e("Failed to read from eeprom!");
            return -1;
        }
        crc = m_crc32(crc, &c, res);
        index += res;
        return c;
    }

    /** @brief Reads several bytes
     * @return the number of bytes read */
    size_t readBytes(char *buffer, size_t length) {
        int res = m_eeprom.read(index, (uint8_t *)buffer, length);
        if (res < 0) {
            log_e("Failed to read from eeprom!");
            return 0;
        }
        crc = m_crc32(crc, (uint8_t *)buffer, res);
        index += res;
        return res;
    }

} m_eeprom_reader;

/**
 * @brief Struct that acts as a custom writer for eeprom with CRC-32 tracking.
 *
 * This struct provides write methods compatible with ArduinoJson's custom Writer interface,
 * enabling automatic calculation of a running CRC-32 value over all data written to eeprom.
 * The writer buffers data to perform page-sized writes for better performance.
 *
 * @see https://arduinojson.org/v6/api/json/serializejson/#custom-writer
 */
static struct {
    size_t index = 0;
    uint32_t crc = 0;

    /** @brief Writes one byte
     * @return the number of bytes written (0 or 1) */
    size_t write(uint8_t c) {
        int res = m_eeprom.buffered_write(index, &c, 1);
        if (res < 0) {
            log_e("Failed to write to eeprom!");
            return 0;
        }
        crc = m_crc32(crc, &c, res);
        index += res;
        return res;
    }

    /** @brief Writes several bytes
     * @return the number of bytes written */
    size_t write(const uint8_t *buffer, size_t length) {
        int res = m_eeprom.buffered_write(index, buffer, length);
        if (res < 0) {
            log_e("Failed to write to eeprom!");
            return 0;
        }
        crc = m_crc32(crc, buffer, res);
        index += res;
        return res;
    }

    /** @brief Flush any pending buffered writes to the EEPROM
     * @return 0 on success, negative error code on failure */
    int flush(void) {
        return m_eeprom.buffer_flush();
    }
} m_eeprom_writer_buffered;

/**
 * @brief Struct that acts as a custom writer for json to compute its CRC-32 hash.
 *
 * This struct provides write methods compatible with ArduinoJson's custom Writer interface,
 * enabling automatic calculation of a running CRC-32 value over all data written to eeprom.
 *
 * @see https://arduinojson.org/v5/doc/tricks/#compute-hash-of-json-output
 */
static struct {
    uint32_t crc = 0;
    size_t write(uint8_t c) {
        crc = m_crc32(crc, &c, 1);
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t length) {
        crc = m_crc32(crc, buffer, length);
        return length;
    }
} m_json_hasher;

/**
 * @brief Load settings from EEPROM using a state machine approach
 *
 * This function implements a robust state machine to load settings from EEPROM
 * with fallback mechanisms. It handles multiple scenarios including corrupted
 * data, missing headers, and EEPROM unavailability. The function uses a
 * dual-copy approach (A and B) for redundancy and data integrity.
 *
 * The state machine performs the following operations:
 * - Validates EEPROM header and CRC
 * - Attempts to load from copy A, then copy B if A fails
 * - Rebuilds headers if necessary
 *
 * @return 0 on successful load, negative error code on failure
 *
 * @note This function is called internally during settings_setup()
 */
static int m_load(void) {
    int res;

    /* State machine */
    enum {
        STATE_EEPROM_HEADER_VALIDATE,
        STATE_EEPROM_HEADER_INJECT,
        STATE_EEPROM_HEADER_REBUILD,
        STATE_EEPROM_LOAD_COPYA,
        STATE_EEPROM_LOAD_COPYB,
        STATE_EEPROM_UNAVAILABLE,
        STATE_FLASH_LOAD,
        STATE_TRANSLATE,
        STATE_DONE,
    } m_load_sm = STATE_EEPROM_HEADER_VALIDATE;
    while (true) {
        switch (m_load_sm) {

            case STATE_EEPROM_HEADER_VALIDATE: {

                /* Skip if eeprom is not available */
                if (m_eeprom_available != true) {
                    log_w("Eeprom not available for loading!");
                    m_load_sm = STATE_EEPROM_UNAVAILABLE;
                    break;
                }

                /* Read eeprom header. The expected layout is as follows:
                 *  [  0] Header Version           (1 byte, uint8_t)                     = 1
                 *  [  1] Header Length            (1 byte, uint8_t)                     = 32
                 *  [  2] EEPROM Total Size        (4 bytes, uint32_t, big-endian)       = 8192
                 *  [  6] EEPROM Page Size         (2 bytes, uint16_t, big-endian)       = 32
                 *  [  8] EEPROM IC Name           (17 bytes, null-terminated char[17])  = "M24C64-FMH6TG"
                 *  [ 25] Copy Count               (1 byte, uint8_t)                     = 2
                 *  [ 26] Reserved                 (5 bytes, for future use and page alignment)
                 *  [ 31] CRC-8                    (1 byte, uint8_t) */
                uint8_t header[CONFIG_SETTINGS_EEPROM_HEADER_SIZE];
                int res = m_eeprom.read(0, header, sizeof(header));
                if (res < 0) {
                    log_e("Failed to read eeprom header!");
                    m_load_sm = STATE_EEPROM_UNAVAILABLE;
                    break;
                }

                /* Parse header fields */
                uint8_t header_version = header[0];
                uint8_t header_length = header[1];
                uint32_t eeprom_size_total = (header[2] << 24) | (header[3] << 16) | (header[4] << 8) | header[5];
                uint16_t eeprom_size_page = (header[6] << 8) | header[7];
                uint8_t copy_count = header[25];
                uint8_t crc8 = header[31];

                /* Validate header crc8 and fields */
                bool header_valid = true;
                uint8_t crc_computed = m_crc8(&header[0], CONFIG_SETTINGS_EEPROM_HEADER_SIZE - 1);
                if (crc8 != crc_computed) {
                    log_e("Invalid eeprom header crc8 (expected 0x%02X, got 0x%02X)!", crc_computed, crc8);
                    header_valid = false;
                }
                if ((header_version != 1) ||
                    (header_length != CONFIG_SETTINGS_EEPROM_HEADER_SIZE)) {
                    log_e("Unexpected eeprom header version or length!");
                    header_valid = false;
                }
                if ((eeprom_size_total != m_eeprom.size_total_get()) ||
                    (eeprom_size_page != m_eeprom.size_page_get())) {
                    log_e("Unexpected eeprom size!");
                    header_valid = false;
                }
                if (copy_count != 2) {
                    log_e("Unexpected copy count!");
                    header_valid = false;
                }

                /* If the header is not valid, we rebuild it
                 * because after all, we might be dealing with a corrupt header with valid data.*/
                if (header_valid == false) {

                    /* Early units had no header but instead a single messagepack document stored at address 0,
                     * so if the first byte suggests a messagepack fixmap, we assume we are dealing with an early unit and attempt to inject a new header. */
                    if ((header[0] >= 0x80) && (header[0] <= 0x8F)) {
                        log_i("Early unit suspected, attempting to inject missing header.");
                        m_load_sm = STATE_EEPROM_HEADER_INJECT;
                        break;
                    } else {
                        m_load_sm = STATE_EEPROM_HEADER_REBUILD;
                        break;
                    }
                }

                /* Compute copy locations */
                size_t eeprom_pages_total = m_eeprom.size_total_get() / m_eeprom.size_page_get();
                size_t eeprom_pages_user_by_header = (CONFIG_SETTINGS_EEPROM_HEADER_SIZE + (m_eeprom.size_page_get() - 1)) / m_eeprom.size_page_get();
                size_t eeprom_pages_left_for_copies = eeprom_pages_total - eeprom_pages_user_by_header;
                size_t eeprom_pages_per_copy = eeprom_pages_left_for_copies / 2;
                m_eeprom_copy[0].start = eeprom_pages_user_by_header * m_eeprom.size_page_get();
                m_eeprom_copy[0].size = eeprom_pages_per_copy * m_eeprom.size_page_get();
                m_eeprom_copy[1].start = m_eeprom_copy[0].start + m_eeprom_copy[0].size;
                m_eeprom_copy[1].size = eeprom_pages_per_copy * m_eeprom.size_page_get();

                /* Move on*/
                m_load_sm = STATE_EEPROM_LOAD_COPYA;
                break;
            }

            case STATE_EEPROM_HEADER_INJECT: {

                /* Deserialize old settings
                 * if not successful or if the document is empty, we give up and build a new header */
                m_eeprom.seek_read(0);
                DeserializationError unpack_res = deserializeMsgPack(m_doc, m_eeprom);
                if (unpack_res != DeserializationError::Ok) {
                    log_w("Failed to deserialize old settings (%s)!", unpack_res.c_str());
                    m_doc.clear();
                    m_load_sm = STATE_EEPROM_HEADER_REBUILD;
                    break;
                } else if (m_doc.memoryUsage() == 0) {
                    log_w("Old settings were empty, skipping.");
                    m_doc.clear();
                    m_load_sm = STATE_EEPROM_HEADER_REBUILD;
                    break;
                }

                /* If schema version is not specified, we force it to 1 */
                if (m_doc["meta"]["schema_version"].is<int>() != true) {
                    m_doc["meta"]["schema_version"] = 1;
                }

                /* Shift old settings after header */
                m_eeprom_writer_buffered.index = CONFIG_SETTINGS_EEPROM_HEADER_SIZE;
                m_eeprom_writer_buffered.crc = 0;
                size_t repack_res = serializeMsgPack(m_doc, m_eeprom_writer_buffered);
                if (repack_res <= 0) {
                    log_e("Failed to shift to eeprom copy A!");
                    m_load_sm = STATE_EEPROM_HEADER_REBUILD;
                    break;
                }
                m_eeprom_writer_buffered.flush();
                m_eeprom.seek_write(m_eeprom_writer_buffered.index);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 24);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 16);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 8);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 0);
                log_i("Successfully shifted old settings to copy A.");

                /* Move on */
                m_load_sm = STATE_EEPROM_HEADER_REBUILD;
                break;
            }

            case STATE_EEPROM_HEADER_REBUILD: {

                /* Log */
                log_i("Rebuilding eeprom header...");

                /* Build header
                 * Note, for now, all solder ninja products are expected to have a a M24C64 eeprom,
                 * but later on this is where we could have fun attempting to detect the eeprom type and size */
                uint8_t header[CONFIG_SETTINGS_EEPROM_HEADER_SIZE] = {0};
                header[0] = 1;                                                     // Header Version
                header[1] = CONFIG_SETTINGS_EEPROM_HEADER_SIZE;                    // Header Length
                header[2] = (((uint32_t)m_eeprom.size_total_get()) >> 24) & 0xFF;  // Eeprom Total Size
                header[3] = (((uint32_t)m_eeprom.size_total_get()) >> 16) & 0xFF;  // Eeprom Total Size
                header[4] = (((uint32_t)m_eeprom.size_total_get()) >> 8) & 0xFF;   // Eeprom Total Size
                header[5] = (((uint32_t)m_eeprom.size_total_get()) >> 0) & 0xFF;   // Eeprom Total Size
                header[6] = (((uint16_t)m_eeprom.size_page_get()) >> 8) & 0xFF;    // Eeprom Page Size
                header[7] = (((uint16_t)m_eeprom.size_page_get()) >> 0) & 0xFF;    // Eeprom Page Size
                strlcpy((char *)&header[8], "M24C64-FMH6TG", 17);                  // Eeprom IC Name
                header[25] = 2;                                                    // Copy Count
                header[31] = m_crc8(&header[0], 31);                               // CRC-8

                /* Write header to eeprom */
                res = m_eeprom.write(0, header, CONFIG_SETTINGS_EEPROM_HEADER_SIZE);
                if (res < 0) {
                    log_e("Failed to write header to eeprom!");
                    m_load_sm = STATE_EEPROM_UNAVAILABLE;
                    break;
                }

                /* Log */
                log_i("Eeprom header rebuilt successfully.");

                /* Compute sizes of copies A and B
                 * Note, we align copies to page boundaries */
                size_t eeprom_pages_total = m_eeprom.size_total_get() / m_eeprom.size_page_get();
                size_t eeprom_pages_user_by_header = (CONFIG_SETTINGS_EEPROM_HEADER_SIZE + (m_eeprom.size_page_get() - 1)) / m_eeprom.size_page_get();
                size_t eeprom_pages_left_for_copies = eeprom_pages_total - eeprom_pages_user_by_header;
                size_t eeprom_pages_per_copy = eeprom_pages_left_for_copies / 2;
                m_eeprom_copy[0].start = eeprom_pages_user_by_header * m_eeprom.size_page_get();
                m_eeprom_copy[0].size = eeprom_pages_per_copy * m_eeprom.size_page_get();
                m_eeprom_copy[1].start = m_eeprom_copy[0].start + m_eeprom_copy[0].size;
                m_eeprom_copy[1].size = eeprom_pages_per_copy * m_eeprom.size_page_get();

                /* Move on */
                m_load_sm = STATE_EEPROM_LOAD_COPYA;
                break;
            }

            case STATE_EEPROM_LOAD_COPYA: {

                /* Log */
                log_i("Loading settings from eeprom copy A...");

                /* Deserialize settings from copy A */
                m_eeprom_reader.index = m_eeprom_copy[0].start;
                m_eeprom_reader.crc = 0;
                DeserializationError unpack_res = deserializeMsgPack(m_doc, m_eeprom_reader);
                if (unpack_res != DeserializationError::Ok) {
                    log_e("Failed to deserialize settings from eeprom copy A (%s)!", unpack_res.c_str());
                    m_doc.clear();
                    m_load_sm = STATE_EEPROM_LOAD_COPYB;
                    break;
                }

                /* Validate CRC */
                uint8_t crc32_stored_bytes[4] = {0};
                res = m_eeprom.read(m_eeprom_reader.index, crc32_stored_bytes, 4);
                if (res < 0) {
                    log_e("Failed to read crc32 from eeprom copy A!");
                    m_doc.clear();
                    m_load_sm = STATE_EEPROM_UNAVAILABLE;
                    break;
                }
                uint32_t crc32_stored = (crc32_stored_bytes[0] << 24) | (crc32_stored_bytes[1] << 16) | (crc32_stored_bytes[2] << 8) | crc32_stored_bytes[3];
                if (crc32_stored != m_eeprom_reader.crc) {
                    log_e("Invalid crc32 in eeprom copy A (expected 0x%08X, got 0x%08X)!", m_eeprom_reader.crc, crc32_stored);
                    m_doc.clear();
                    m_load_sm = STATE_EEPROM_LOAD_COPYB;
                    break;
                }

                /* Log */
                log_i("Successfully loaded settings from eeprom copy A.");

                /* Move on */
                m_load_sm = STATE_TRANSLATE;
                break;
            }

            case STATE_EEPROM_LOAD_COPYB: {

                /* Log */
                log_i("Loading settings from eeprom copy B...");

                /* Deserialize settings from copy B */
                m_eeprom_reader.index = m_eeprom_copy[1].start;
                m_eeprom_reader.crc = 0;
                DeserializationError unpack_res = deserializeMsgPack(m_doc, m_eeprom_reader);
                if (unpack_res != DeserializationError::Ok) {
                    log_e("Failed to deserialize settings from eeprom copy B (%s)!", unpack_res.c_str());
                    m_doc.clear();
                    m_load_sm = STATE_FLASH_LOAD;
                    break;
                }

                /* Validate CRC */
                uint8_t crc32_stored_bytes[4] = {0};
                res = m_eeprom.read(m_eeprom_reader.index, crc32_stored_bytes, 4);
                if (res < 0) {
                    log_e("Failed to read crc32 from eeprom copy B!");
                    m_doc.clear();
                    m_load_sm = STATE_EEPROM_UNAVAILABLE;
                    break;
                }
                uint32_t crc32_stored = (crc32_stored_bytes[0] << 24) | (crc32_stored_bytes[1] << 16) | (crc32_stored_bytes[2] << 8) | crc32_stored_bytes[3];
                if (crc32_stored != m_eeprom_reader.crc) {
                    log_e("Invalid crc32 in eeprom copy B (expected 0x%08X, got 0x%08X)!", m_eeprom_reader.crc, crc32_stored);
                    m_doc.clear();
                    m_load_sm = STATE_FLASH_LOAD;
                    break;
                }

                /* Log */
                log_i("Successfully loaded settings from eeprom copy B.");

                /* Move on */
                m_load_sm = STATE_TRANSLATE;
                break;
            }

            case STATE_EEPROM_UNAVAILABLE: {

                /* Flag eeprom as unavailable */
                m_eeprom_available = false;

                /* Move on */
                m_load_sm = STATE_FLASH_LOAD;
                break;
            }

            case STATE_FLASH_LOAD: {

                /* Log */
                log_i("Loading settings from flash...");

                /* Skip if flash is not available */
                if (m_flash_available != true) {
                    log_e("Flash is not available!");
                    m_load_sm = STATE_DONE;
                    return -1;  // TODO: Improve error handling?
                }

                /* Gain access to flash */
                if (m_flash_accessor != FLASH_ACCESSOR_NONE) {
                    log_e("Flash is already in use!");
                    m_load_sm = STATE_DONE;
                    return -1;  // TODO: Improve error handling?
                }
                m_flash_accessor = FLASH_ACCESSOR_LOCAL;

                /* Ensure file exists in flash */
                if (!FatFS.exists("settings.json")) {
                    log_e("Flash doesn't contain settings.json!");
                    m_flash_accessor = FLASH_ACCESSOR_NONE;
                    m_load_sm = STATE_DONE;
                    return -1;  // TODO: Improve error handling?
                }

                /* Open file in flash */
                File fp = FatFS.open("settings.json", "r");
                if (!fp) {
                    log_e("Failed to open settings.json for reading!");
                    m_flash_accessor = FLASH_ACCESSOR_NONE;
                    m_load_sm = STATE_DONE;
                    return -1;  // TODO: Improve error handling?
                }

                /* Deserialize settings from flash */
                DeserializationError unpack_res = deserializeJson(m_doc, fp);
                if (unpack_res != DeserializationError::Ok) {
                    log_e("Failed to deserialize settings from flash (%s)!", unpack_res.c_str());
                    fp.close();
                    m_flash_accessor = FLASH_ACCESSOR_NONE;
                    m_load_sm = STATE_DONE;
                    return -1;  // TODO: Improve error handling?
                }

                /* Close file in flash */
                fp.close();

                /* Release access to flash */
                m_flash_accessor = FLASH_ACCESSOR_NONE;

                /* Mark settings as modified so that they will be saved */
                m_modified = true;
                m_modified_immediate = true;

                /* Log */
                log_i("Successfully loaded settings from flash.");

                /* Move on */
                m_load_sm = STATE_TRANSLATE;
                break;
            }

            case STATE_TRANSLATE: {

                /* Dirty fix to ensure m_doc is usable for writing after a failed deserialization */
                if (m_doc.memoryUsage() == 0) {
                    static const char fix[] = "{}";
                    DeserializationError unpack_res = deserializeJson(m_doc, (char *)(&fix[0]));
                    if (unpack_res != DeserializationError::Ok) {
                        log_e("Failed to create empty document (%d)!", unpack_res.code());
                        m_doc.clear();
                        m_load_sm = STATE_DONE;
                        break;
                    }
                }

                /* Found expected schema version */
                if (m_doc["meta"]["schema_version"].as<int>() == 2) {
                    log_i("Found expected schema version.");
                }

                /* Migrate from old settings to new settings */
                else if (m_doc["meta"]["schema_version"].as<int>() == 1) {
                    log_i("Translating schema version 1 to 2.");

                    /* Extract useful information from old settings */
                    char old_product_number[CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH + CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH + 1] = {0};
                    char old_product_revision[CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH + 1] = {0};
                    char old_serial_number[CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH + 1] = {0};
                    char old_user_name_line1[CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH + 1] = {0};
                    char old_user_name_line2[CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH + 1] = {0};
                    uint8_t old_user_icon[32] = {0};
                    strlcpy(old_product_number, m_doc["product"]["product_number"] | "SLTO00001R8A", sizeof(old_product_number));
                    strlcpy(old_serial_number, m_doc["product"]["serial_number"] | "0000000", sizeof(old_serial_number));
                    strlcpy(old_user_name_line1, m_doc["user"]["name"][0] | "", sizeof(old_user_name_line1));
                    strlcpy(old_user_name_line2, m_doc["user"]["name"][1] | "", sizeof(old_user_name_line2));
                    if (m_doc["user"]["icon"].size() == 32) {
                        for (size_t i = 0; i < 32; i++) {
                            old_user_icon[i] = m_doc["user"]["icon"][i];
                        }
                    }

                    /* Split old product number field into product number and revision */
                    if (strlen(old_product_number) > CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH) {
                        strlcpy(old_product_revision, &old_product_number[CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH], sizeof(old_product_revision));
                        old_product_number[CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH] = '\0';
                    }

                    /* Clear and rebuild settings in ram */
                    m_doc.clear();
                    m_doc["meta"]["schema_version"] = 2;
                    m_doc["product"]["number"] = (char *)old_product_number;
                    m_doc["product"]["revision"] = (char *)old_product_revision;
                    m_doc["product"]["serial"] = (char *)old_serial_number;
                    m_doc["preferences"]["user"]["name"][0] = (char *)old_user_name_line1;
                    m_doc["preferences"]["user"]["name"][1] = (char *)old_user_name_line2;
                    for (size_t i = 0; i < 32; i++) {
                        m_doc["preferences"]["user"]["icon"][i] = old_user_icon[i];
                    }

                    /* Mark settings as modified so that they will be saved */
                    m_modified = true;
                    m_modified_immediate = true;
                }

                /* Unknown settings schema version */
                else {
                    log_w("Unexpected schema version (%d), use at your own risk!", m_doc["meta"]["schema_version"].as<int>());
                }

                /* Move on */
                m_load_sm = STATE_DONE;
                break;
            }

            case STATE_DONE: {
                return 0;
            }

            default: {
                log_e("Hurray, we found a cosmic ray!");
                return -1;
            }
        }
    }
}

/**
 * @brief Save settings to EEPROM using dual-copy redundancy
 *
 * This function saves the current settings to EEPROM using a dual-copy approach
 * for data integrity and redundancy. It serializes the internal JSON document
 * to MessagePack format and writes it to both copy A and copy B locations in
 * EEPROM. The function includes CRC validation and size checking to ensure
 * data integrity.
 *
 * The save process includes:
 * - Size validation to ensure data fits in EEPROM copies
 * - Serialization of JSON document to MessagePack format
 * - CRC-32 calculation for data integrity
 * - Writing to both copy A and copy B locations
 *
 * @return 0 on successful save, negative error code on failure
 *
 * @note This function is called internally by settings_task() when settings are modified
 */
static int m_save(void) {

    /* If schema version is not specified, we force it to 1 */
    if (m_doc["meta"]["schema_version"].is<int>() != true) {
        m_doc["meta"]["schema_version"] = 2;
    }

    /* State machine */
    bool eeprom_save_success = false;
    bool flash_save_success = false;
    bool flash_file_exists = false;
    uint32_t flash_file_crc = 0;
    enum {
        STATE_EEPROM_0,
        STATE_EEPROM_1,
        STATE_EEPROM_2,
        STATE_FLASH_0,
        STATE_FLASH_1,
        STATE_FLASH_2,
        STATE_DONE,
    } m_save_sm = STATE_EEPROM_0;
    while (true) {
        switch (m_save_sm) {

            case STATE_EEPROM_0: {

                /* Skip eeprom if not available */
                if (m_eeprom_available != true) {
                    log_w("Eeprom not available for saving!");
                    m_save_sm = STATE_FLASH_0;
                    break;
                }

                /* Log */
                log_i("Saving settings to eeprom...");

                /* Ensure settings are not too large for eeprom copies */
                size_t repack_res = measureMsgPack(m_doc);
                if ((repack_res > m_eeprom_copy[0].size - 4) || (repack_res > m_eeprom_copy[1].size - 4)) {
                    log_e("Settings too large for eeprom copies!");
                    m_save_sm = STATE_FLASH_0;
                    break;
                }

                /* Move on */
                m_save_sm = STATE_EEPROM_1;
                break;
            }

            case STATE_EEPROM_1: {

                /* First, save to copy B */
                m_eeprom_writer_buffered.index = m_eeprom_copy[1].start;
                m_eeprom_writer_buffered.crc = 0;
                size_t repack_res = serializeMsgPack(m_doc, m_eeprom_writer_buffered);
                if (repack_res <= 0) {
                    log_e("Failed to save settings to copy B!");
                    m_save_sm = STATE_EEPROM_2;
                    break;
                }
                m_eeprom_writer_buffered.flush();
                m_eeprom.seek_write(m_eeprom_writer_buffered.index);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 24);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 16);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 8);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 0);
                eeprom_save_success = true;

                /* Log */
                log_i("Settings saved to eeprom copy B.");

                /* Move on */
                m_save_sm = STATE_EEPROM_2;
                break;
            }

            case STATE_EEPROM_2: {

                /* Second, save to copy A */
                m_eeprom_writer_buffered.index = m_eeprom_copy[0].start;
                m_eeprom_writer_buffered.crc = 0;
                size_t repack_res = serializeMsgPack(m_doc, m_eeprom_writer_buffered);
                if (repack_res <= 0) {
                    log_e("Failed to save settings to copy A!");
                    return -EIO;
                }
                m_eeprom_writer_buffered.flush();
                m_eeprom.seek_write(m_eeprom_writer_buffered.index);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 24);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 16);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 8);
                m_eeprom.write(m_eeprom_writer_buffered.crc >> 0);
                eeprom_save_success = true;

                /* Log */
                log_i("Settings saved to eeprom copy A.");

                /* Move on */
                m_save_sm = STATE_FLASH_0;
                break;
            }

            case STATE_FLASH_0: {

                /* Skip flash if not available */
                if (m_flash_available != true) {
                    log_w("Flash not available for saving!");
                    m_save_sm = STATE_DONE;
                    break;
                }

                /* Log */
                log_i("Saving settings to flash...");

                /* Gain access to flash */
                if (m_flash_accessor != FLASH_ACCESSOR_NONE) {
                    log_w("Flash is already in use!");
                    m_save_sm = STATE_DONE;
                    break;
                }
                m_flash_accessor = FLASH_ACCESSOR_LOCAL;

                /* Ensure file exists in flash */
                if (!FatFS.exists("settings.json")) {
                    flash_file_exists = false;
                    m_save_sm = STATE_FLASH_1;
                    break;
                } else {
                    flash_file_exists = true;
                }

                /* Compute hash of file in flash */
                File fp = FatFS.open("settings.json", "r");
                if (!fp) {
                    log_e("Failed to open settings.json for reading!");
                    m_save_sm = STATE_FLASH_2;
                    break;
                }
                while (fp.available()) {
                    uint8_t c = fp.read();
                    flash_file_crc = m_crc32(flash_file_crc, &c, 1);
                }
                fp.close();

                /* Move on */
                m_save_sm = STATE_FLASH_1;
                break;
            }

            case STATE_FLASH_1: {

                /* Create a filtered document without diagnostics for flash storage */
                DynamicJsonDocument doc_filtered(CONFIG_SETTINGS_JSON_DOCUMENT_SIZE);
                doc_filtered["meta"] = m_doc["meta"];
                doc_filtered["product"] = m_doc["product"];
                doc_filtered["preferences"] = m_doc["preferences"];
                doc_filtered["factory"] = m_doc["factory"];

                /* Because we are trying to be mindful of the flash wear,
                 * if the file exsits we only want to write it if the content is different,
                 * and for that we compare the two crc-32 values */
                if (flash_file_exists == true) {

                    /* Compute hash of the filtered document */
                    m_json_hasher.crc = 0;
                    size_t json_res = serializeJsonPretty(doc_filtered, m_json_hasher);
                    if (json_res <= 0) {
                        log_e("Failed to compute hash of settings!");
                        m_save_sm = STATE_FLASH_2;
                        break;
                    }

                    /* Skip writing if the content is the same */
                    if (flash_file_crc == m_json_hasher.crc) {
                        log_i("Settings are the same, skipping write!");
                        flash_save_success = true;
                        m_save_sm = STATE_FLASH_2;
                        break;
                    }
                }

                /* Open file for writing */
                File write_file = FatFS.open("settings.json", "w");
                if (!write_file) {
                    log_e("Failed to open settings.json for writing!");
                    m_save_sm = STATE_FLASH_2;
                    break;
                }

                /* Write document to file */
                size_t bytes_written = serializeJsonPretty(doc_filtered, write_file);
                if (bytes_written <= 0) {
                    log_w("Failed to save settings to flash!");
                }
                write_file.close();
                flash_save_success = true;

                /* Log */
                log_i("Settings saved to flash.");

                /* Move on */
                m_save_sm = STATE_FLASH_2;
                break;
            }

            case STATE_FLASH_2: {

                /* Release access to flash */
                m_flash_accessor = FLASH_ACCESSOR_NONE;

                /* Move on */
                m_save_sm = STATE_DONE;
                break;
            }

            case STATE_DONE: {
                if ((eeprom_save_success == true) ||
                    (flash_save_success == true)) {
                    return 0;
                } else {
                    return -1;
                }
            }

            default: {
                log_e("Hurray, we found a cosmic ray!");
                return -1;
            }
        }
    }
}

/**
 * @brief Initialize the settings module and load existing settings
 *
 * This function performs the initial setup of the settings module by:
 * - Initializing the EEPROM interface
 * - Setting up internal state variables
 * - Attempting to load existing settings from EEPROM (non-critical if it fails)
 *
 * @return 0 on successful initialization, negative error code on failure
 */
int settings_setup(void) {
    int res;

    /* Setup eeprom */
    res = m_eeprom.setup(Wire, 0x50);
    if (res < 0) {
        log_e("Failed to setup eeprom!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Initialize working variables */
    m_loaded = false;
    m_modified = false;
    m_modified_immediate = false;
    m_modified_timestamp = 0;

    /* Detect eeprom */
    if (m_eeprom.detect() != true) {
        log_e("Failed to detect eeprom!");
        m_eeprom_available = false;
    } else {
        m_eeprom_available = true;
    }

    /* Setup fatfs */
    bool fatfs_res = FatFS.begin();
    if (fatfs_res != true) {
        log_w("Failed to init fatfs!");
        m_flash_available = false;
        m_flash_accessor = FLASH_ACCESSOR_NONE;
    } else {
        m_flash_available = true;
        m_flash_accessor = FLASH_ACCESSOR_NONE;
    }

    /* Load settings if possible, but don't fail if it fails */
    res = m_load();
    if (res < 0) {
        log_w("Failed to load settings!");
    }

    /* Return success */
    return 0;
}

/**
 * @brief Read data from the eeprom memory
 *
 * This function provides a low-level interface to read raw data from the eeprom.
 *
 * @param address The starting address in eeprom to read from
 * @param data    Pointer to buffer where read data will be stored
 * @param length  Number of bytes to read from eeprom
 * @return number of bytes successfully read, or negative error code on failure
 */
int settings_eeprom_read(const size_t address, uint8_t *const data, const size_t length) {
    return m_eeprom.read(address, data, length);
}

/**
 * @brief Write data to the eeprom memory
 *
 * This function provides a low-level interface to write raw data to the eeprom.
 *
 * @param address The starting address in eeprom to write to
 * @param data    Pointer to buffer containing data to write
 * @param length  Number of bytes to write to eeprom
 * @return number of bytes successfully written, or negative error code on failure
 */
int settings_eeprom_write(const size_t address, const uint8_t *const data, const size_t length) {
    return m_eeprom.write(address, data, length);
}

/**
 * @brief Completely erase all eeprom contents and reset settings to defaults
 *
 * This function performs a complete wipe of the eeprom memory by writing 0xFF
 * to all memory locations. This operation is irreversible and will permanently
 * delete all stored settings and configuration data.
 *
 * @warning This function takes around 1 second to complete. Call with caution
 * as it won't play nice with code that requires strict timing.
 *
 * @return 0 on successful completion, negative error code on failure
 */
int settings_eeprom_wipe(void) {
    int res;

    /* Wipe eeprom contents
     * @note This may take around 1 second to complete */
    uint8_t chunk[m_eeprom.size_page_get()];
    memset(chunk, 0xFF, sizeof(chunk));
    for (size_t i = 0; i < m_eeprom.size_total_get(); i += sizeof(chunk)) {
        res = m_eeprom.write(i, chunk, sizeof(chunk));
        if (res < 0) {
            log_e("Failed to clear eeprom page!");
            return -EIO;
        }
    }

    /* Return success */
    return 0;
}

/**
 * @brief Completely wipe all contents of the FatFS flash filesystem
 *
 * This function performs a complete wipe of the flash filesystem by formatting it.
 *
 * @return 0 on successful completion, negative error code on failure
 * @retval -EBUSY Flash is currently in use by another operation
 * @retval -EIO Flash operation failed
 */
int settings_flash_wipe(void) {

    /* Skip if flash is not available */
    if (m_flash_available != true) {
        log_w("Flash not available for wiping!");
        return -EIO;
    }

    /* Ensure flash is not in use */
    if (m_flash_accessor != FLASH_ACCESSOR_NONE) {
        log_e("Flash is already in use!");
        return -EBUSY;
    }

    /* Gain exclusive access to flash */
    m_flash_accessor = FLASH_ACCESSOR_LOCAL;

    /* Log */
    log_i("Wiping flash filesystem...");

    /* Try to format the filesystem to wipe all contents */
    if (FatFS.format() != true) {
        /* If format() failed, fall back to removing all files */
        log_w("Format failed, attempting to remove all files...");

        /* Open root directory and remove all files */
        Dir root = FatFS.openDir("/");
        root.rewind();
        while (root.next()) {
            String fileName = root.fileName();
            if (root.isFile()) {
                if (!FatFS.remove(fileName.c_str())) {
                    log_w("Failed to remove file: %s", fileName.c_str());
                } else {
                    log_i("Removed file: %s", fileName.c_str());
                }
            } else if (root.isDirectory()) {
                /* Remove directory and its contents */
                if (!FatFS.rmdir(fileName.c_str())) {
                    log_w("Failed to remove directory: %s", fileName.c_str());
                } else {
                    log_i("Removed directory: %s", fileName.c_str());
                }
            }
        }
    } else {
        log_i("Flash filesystem formatted successfully.");
    }

    /* Release access to flash */
    m_flash_accessor = FLASH_ACCESSOR_NONE;

    /* Log */
    log_i("Flash filesystem wiped successfully.");

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param[out] temperature_c
 * @return
 */
int settings_temperature_target_get(float &temperature_c) {

    /* Fetch target temperature */
    if (m_doc["preferences"]["heating"]["temperature_c"].is<float>() != true) {
        return 0;
    }
    float t = m_doc["preferences"]["heating"]["temperature_c"];
    if (t < CONFIG_CONTROLLER_TARGET_MIN || t > CONFIG_CONTROLLER_TARGET_MAX_SAFE) {
        return 0;
    }
    temperature_c = t;

    /* Return found */
    return 1;
}

/**
 * @brief
 * @param[in] temperature_c
 * @return
 */
int settings_temperature_target_set(const float target_c) {

    /* Update json document */
    m_doc["preferences"]["heating"]["temperature_c"] = target_c;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param[out] icon
 * @param[out] line1
 * @param[out] line2
 * @return
 */
int settings_user_get(uint8_t *const icon, char *const line1, char *const line2) {

    /* Fetch icon */
    size_t icon_size = m_doc["preferences"]["user"]["icon"].size();
    if (icon_size != 32) {
        return 0;
    }
    for (size_t i = 0; i < icon_size; i++) {
        icon[i] = m_doc["preferences"]["user"]["icon"][i];
    }

    /* Fetch user name */
    const char *l1 = m_doc["preferences"]["user"]["name"][0];
    const char *l2 = m_doc["preferences"]["user"]["name"][1];
    if ((strlen(l1) > CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH) ||
        (strlen(l2) > CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH)) {
        return 0;
    }
    if ((strlen(l1) + strlen(l2)) < 1) {
        return 0;
    }
    if (line1 != NULL) {
        strlcpy(line1, l1, CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH + 1);
    }
    if (line2 != NULL) {
        strlcpy(line2, l2, CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH + 1);
    }

    /* Return found */
    return 1;
}

/**
 * @brief
 * @param[in] icon
 * @param[in] line1
 * @param line2
 * @return
 */
int settings_user_set(const uint8_t icon[32], const char *line1, const char *line2) {

    /* Update json document */
    for (size_t i = 0; i < 32; i++) {
        m_doc["preferences"]["user"]["icon"][i] = icon[i];
    }
    m_doc["preferences"]["user"]["name"][0] = (char *)line1;
    m_doc["preferences"]["user"]["name"][1] = (char *)line2;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_immediate = true;

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param[out] number
 * @param[out] revision
 * @param[out] serial
 * @return
 */
int settings_product_get(char *const number, char *const revision, char *const serial) {

    /* Fetch values */
    const char *pn = m_doc["product"]["number"];
    const char *rv = m_doc["product"]["revision"];
    const char *sn = m_doc["product"]["serial"];
    if (((m_doc["product"].containsKey("number") != true) || (strlen(pn) > CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH)) ||
        ((m_doc["product"].containsKey("revision") != true) || (strlen(rv) > CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH)) ||
        ((m_doc["product"].containsKey("serial") != true) || (strlen(sn) > CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH))) {
        return 0;
    }
    if (number != NULL) {
        strlcpy(number, pn, CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH + 1);
    }
    if (revision != NULL) {
        strlcpy(revision, rv, CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH + 1);
    }
    if (serial != NULL) {
        strlcpy(serial, sn, CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH + 1);
    }

    /* Return found */
    return 1;
}

/**
 * @brief
 * @param[in] number
 * @param[in] revision
 * @param[in] serial
 * @return
 */
int settings_product_set(const char *const number, const char *const revision, const char *const serial) {

    /* Ensure valid product number and serial number */
    if ((strlen(number) > CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH) ||
        (strlen(revision) > CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH) ||
        (strlen(serial) > CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH)) {
        return -EINVAL;
    }

    /* Update json document */
    m_doc["product"]["number"] = (char *)number;
    m_doc["product"]["revision"] = (char *)revision;
    m_doc["product"]["serial"] = (char *)serial;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_immediate = true;

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param[out] fahrenheit
 * @return
 */
int settings_interface_units_get(bool &fahrenheit) {

    /* Return if found */
    if (m_doc["preferences"]["ui"]["units"].is<int>() == true) {
        fahrenheit = (m_doc["preferences"]["ui"]["units"] == 1);
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param[in] fahrenheit
 * @return
 */
int settings_interface_units_set(const bool fahrenheit) {

    /* Update json document */
    m_doc["preferences"]["ui"]["units"] = fahrenheit ? 1 : 0;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param[out] left_handed
 * @return
 */
int settings_interface_rotation_get(bool &left_handed) {

    /* Return if found */
    if (m_doc["preferences"]["ui"]["handedness"].is<int>() == true) {
        left_handed = (m_doc["preferences"]["ui"]["handedness"] == 1);
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param left_handed
 * @return
 */
int settings_interface_rotation_set(const bool left_handed) {

    /* Update json document */
    m_doc["preferences"]["ui"]["handedness"] = (left_handed) ? 1 : 0;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param percent
 * @return
 */
int settings_display_brightness_get(int &percent) {

    /* Return if found */
    if (m_doc["preferences"]["ui"]["brightness"].is<int>() == true) {
        percent = m_doc["preferences"]["ui"]["brightness"];
        if (percent < 10) {
            percent = 10;
        } else if (percent > 100) {
            percent = 100;
        }
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param percent
 * @return
 */
int settings_display_brightness_set(const int percent) {

    /* Ensure valid brightness */
    if ((percent < 10) ||
        (percent > 100)) {
        return -EINVAL;
    }

    /* Update json document */
    m_doc["preferences"]["ui"]["brightness"] = percent;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief Get accelerometer idle time setting
 * @param[out] time_ms Idle time in milliseconds
 * @return 1 if setting was found, 0 if not found (use default)
 */
int settings_accelerometer_idle_time_get(uint32_t &time_ms) {

    /* Return if found */
    if (m_doc["preferences"]["accelerometer"]["idle_time_ms"].is<uint32_t>() == true) {
        time_ms = m_doc["preferences"]["accelerometer"]["idle_time_ms"];
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief Get the total heating time in seconds
 * @param[out] seconds Total heating time in seconds
 * @return 1 if found, 0 if not found
 */
int settings_diagnostics_heating_time_get(uint32_t &seconds) {

    /* Return if found */
    if (m_doc["diagnostics"]["heating"]["time"].is<uint32_t>() == true) {
        seconds = m_doc["diagnostics"]["heating"]["time"];
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief Increment the heating time in seconds
 * @param[in] seconds Number of seconds to increment
 * @return 0 in case of success, or a negative error code otherwise
 */
int settings_diagnostics_heating_time_increment(const uint32_t seconds) {

    /* Update json document */
    m_doc["diagnostics"]["heating"]["time"] = m_doc["diagnostics"]["heating"]["time"].as<uint32_t>() + seconds;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_immediate = true;

    /* Return success */
    return 0;
}

/**
 * @brief Set accelerometer idle time setting
 * @param[in] time_ms Idle time in milliseconds
 * @return 0 on success, negative error code on failure
 */
int settings_accelerometer_idle_time_set(const uint32_t time_ms) {

    /* Update json document */
    m_doc["preferences"]["accelerometer"]["idle_time_ms"] = time_ms;

    /* Mark settings as modified */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief Get the maximum USB voltage in volts
 * @param[out] voltage Maximum USB voltage in volts
 * @return 1 if found, 0 if not found
 */
int settings_diagnostics_usb_voltage_max_get(float &voltage) {

    /* Return if found */
    if (m_doc["diagnostics"]["usb"]["voltage_max"].is<float>() == true) {
        voltage = m_doc["diagnostics"]["usb"]["voltage_max"];
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief Report and update the maximum USB voltage if higher than current value
 * @param[in] voltage USB voltage in volts to report
 * @return 0 on success
 */
int settings_diagnotics_usb_voltage_max_report(const float voltage) {

    /* Update local value only if higher */
    if (voltage > m_doc["diagnostics"]["usb"]["voltage_max"].as<float>()) {
        m_doc["diagnostics"]["usb"]["voltage_max"] = voltage;
        m_modified = true;
        m_modified_immediate = true;
    }

    /* Return success */
    return 0;
}

/**
 * @brief Wipe all settings.
 * @return 0 on success, negative error code on failure
 * @note This function will wipe all settings, including the important ones such as product information.
 */
int settings_wipe(void) {

    /* Clear json document */
    m_doc.clear();

    /* Mark settings as modified */
    m_modified = true;
    m_modified_immediate = true;

    /* Return success */
    return 0;
}

/**
 * @brief Background task for managing settings persistence
 *
 * This function should be called periodically from the main loop to handle
 * deferred saving of settings to EEPROM. It checks if settings have been
 * modified and either the immediate save flag is set or the configured
 * delay has elapsed, then triggers a save operation.
 *
 * The function implements a deferred write mechanism to avoid excessive
 * EEPROM wear by batching multiple setting changes into a single write
 * operation after a configurable delay period.
 *
 * @return 0 on successful execution, negative error code on failure
 * @retval 0 Success - task completed (may have saved settings or done nothing)
 * @note This function is non-blocking and safe to call frequently
 */
int settings_task(void) {
    int res;

    /* Handle deferred saving of settings */
    if ((m_modified == true) &&
        ((m_modified_immediate == true) || ((millis() - m_modified_timestamp) >= CONFIG_SETTINGS_EEPROM_DEFFERED_WRITE_DELAY_MS))) {

        /* Clear modified flag regardles of success or failure */
        m_modified = false;
        m_modified_immediate = false;

        /* Attempt to save settings */
        res = m_save();
        if (res < 0) {
            log_w("Failed to save settings!");
        }
    }

    /* Return success */
    return 0;
}
