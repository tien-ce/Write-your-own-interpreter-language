/*
 * Example: using the bytes value type from C native code.
 *
 *  - modbus_read_request(slave, address, count) builds a Modbus RTU "read holding registers"
 *    frame and RETURNS bytes to the script (C -> script, filled in place, no extra copy).
 *  - crc16(data) takes bytes FROM the script and reads them without copying (script -> C).
 */
#include "builtin.h"
#include <stdint.h>
#include <stddef.h>

/* -------------------- Constants -------------------- */

/* Modbus function code: read holding registers */
#define MODBUS_FUNC_READ_HOLDING 0x03

/* Frame layout: slave, function, address (2), count (2), CRC (2) */
#define MODBUS_REQUEST_LENGTH 8

/* Protocol limits */
#define MODBUS_MAX_SLAVE 247
#define MODBUS_MAX_REGISTER_COUNT 125

/* -------------------- Static Function Prototypes -------------------- */

static uint16_t modbus_crc16(const uint8_t *data, size_t length);
static value_t *builtin_modbus_read_request(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_crc16(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Compute the Modbus RTU CRC-16 (polynomial 0xA001, initial value 0xFFFF).
 * @param data Bytes to checksum.
 * @param length Number of bytes.
 * @return The CRC; on the wire it is sent low byte first.
 */
static uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    /* Fold each byte into the running CRC, one bit at a time */
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/**
 * @brief Built-in modbus_read_request(int slave, int address, int count): returns bytes.
 * Usage in a Ti script: bytes frame = modbus_read_request(1, 0, 10);
 * @param handle Calling runtime handle, used to report argument errors.
 * @param argv Array of argument values (slave, address, count), already type-checked as int.
 * @param argc Number of arguments passed.
 * @return Newly allocated 8-byte bytes value, or NULL after raising an error.
 */
static value_t *builtin_modbus_read_request(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    int slave = argv[0]->int_val;
    int address = argv[1]->int_val;
    int count = argv[2]->int_val;

    /* Validate against the protocol limits; the interpreter only checked that they are ints */
    if (slave < 1 || slave > MODBUS_MAX_SLAVE) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "modbus_read_request: slave must be 1..%d, got %d",
                       MODBUS_MAX_SLAVE, slave);
        return NULL;
    }
    if (address < 0 || address > 0xFFFF) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "modbus_read_request: address must be 0..65535, got %d",
                       address);
        return NULL;
    }
    if (count < 1 || count > MODBUS_MAX_REGISTER_COUNT) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "modbus_read_request: count must be 1..%d, got %d",
                       MODBUS_MAX_REGISTER_COUNT, count);
        return NULL;
    }

    /* Allocate the result and get a writable pointer to its storage: the frame is built in place */
    uint8_t *frame = NULL;
    value_t *result = val_new_bytes_alloc(MODBUS_REQUEST_LENGTH, &frame);
    if (result == NULL || frame == NULL) {
        val_free(result);
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "modbus_read_request: cannot allocate the frame");
        return NULL;
    }

    /* Header and payload fields are big-endian */
    frame[0] = (uint8_t)slave;
    frame[1] = MODBUS_FUNC_READ_HOLDING;
    frame[2] = (uint8_t)(address >> 8);
    frame[3] = (uint8_t)(address & 0xFF);
    frame[4] = (uint8_t)(count >> 8);
    frame[5] = (uint8_t)(count & 0xFF);

    /* The CRC covers the first six bytes and is appended low byte first */
    uint16_t crc = modbus_crc16(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);

    /* Ownership of the value passes to the interpreter */
    return result;
}

/**
 * @brief Built-in crc16(bytes data): returns the Modbus CRC-16 of the buffer as an int.
 * Usage in a Ti script: int crc = crc16(frame);
 * Shows how a native reads a bytes argument: the pointer is borrowed for the duration of the call
 * and nothing is copied; a native that needs the data later must copy it.
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values (data), already type-checked as bytes.
 * @param argc Number of arguments passed.
 * @return Newly allocated int value (0..65535), or NULL after raising an error.
 */
static value_t *builtin_crc16(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    /* Borrow the buffer: valid until this function returns, never freed here */
    const uint8_t *data = val_bytes_data(argv[0]);
    int length = val_bytes_len(argv[0]);

    uint16_t crc = modbus_crc16(data, (size_t)length);

    value_t *result = val_new_int((int)crc);
    if (result == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "crc16: cannot allocate the result");
    }
    return result;
}

/* -------------------- Public Functions -------------------- */

/* Register modbus_read_request() and crc16() */
void builtin_bytes_register(void)
{
    static param_t request_params[] = {
        { VAL_INT, "slave", VAL_NULL },
        { VAL_INT, "address", VAL_NULL },
        { VAL_INT, "count", VAL_NULL },
    };
    static param_t crc_params[] = { { VAL_BYTES, "data", VAL_NULL } };

    /* The return type is VAL_BYTES, so scripts can assign the result to a 'bytes' variable */
    register_builtin_function("modbus_read_request", VAL_BYTES, request_params, 3, builtin_modbus_read_request);
    register_builtin_function("crc16", VAL_INT, crc_params, 1, builtin_crc16);
}
