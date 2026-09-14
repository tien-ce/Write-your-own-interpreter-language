#ifndef TI_TYPE_H
#define TI_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Canonical Type Definitions -------------------- */

/**
 * @brief Canonical data type enumeration for both AST syntax and runtime values.
 */
typedef enum {
    VAL_NULL,    // Null / uninitialized / empty return
    VAL_INT,     // Integer type
    VAL_FLOAT,   // Floating-point type
    VAL_STRING,  // String type
    VAL_BOOL,    // Boolean type
    VAL_VOID,    // Void return type for functions
} val_type_t;

/* Backward-compatibility alias */
typedef val_type_t value_type_t;

#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_H */
