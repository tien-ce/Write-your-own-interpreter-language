#include <Arduino.h>
#include <TienInterpreter.h>

/* -------------------- Static Callbacks -------------------- */

/**
 * @brief Arduino Serial logging callback function.
 */
static void arduino_log_callback(const char *fmt, va_list args)
{
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    Serial.print(buffer);
}

/**
 * @brief Arduino fatal error callback function (halts execution).
 */
static void arduino_fatal_callback(void)
{
    Serial.println("[FATAL] Interpreter error! Halting system.");
    while (1)
    {
        delay(1000);
    }
}

/* -------------------- Static Built-in Functions -------------------- */

/**
 * @brief Native print function for Ti scripts via Arduino Serial.
 */
static value_t *built_in_print(value_t **argv, int argc)
{
    if (argc == 0)
    {
        Serial.println();
    }
    for (int i = 0; i < argc; i++)
    {
        switch (argv[i]->type)
        {
            case VAL_STRING:
                Serial.print(argv[i]->string_val);
                break;
            case VAL_INT:
                Serial.print(argv[i]->int_val);
                break;
            case VAL_FLOAT:
                Serial.print(argv[i]->float_val);
                break;
            case VAL_BOOL:
                Serial.print(argv[i]->bool_val ? "true" : "false");
                break;
            default:
                Serial.print("[Unknown]");
                break;
        }
    }
    return val_new_null();
}

/**
 * @brief Native delay function for Ti scripts (e.g. delay(500)).
 */
static value_t *built_in_delay(value_t **argv, int argc)
{
    if (argc >= 1 && argv[0]->type == VAL_INT)
    {
        delay(argv[0]->int_val);
    }
    return val_new_null();
}

/**
 * @brief Native digitalWrite function for Ti scripts (e.g. digitalWrite(13, 1)).
 */
static value_t *built_in_digital_write(value_t **argv, int argc)
{
    if (argc >= 2 && argv[0]->type == VAL_INT && argv[1]->type == VAL_INT)
    {
        digitalWrite(argv[0]->int_val, argv[1]->int_val);
    }
    return val_new_null();
}

/* -------------------- Arduino Setup & Loop -------------------- */

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        ; // Wait for serial port to connect (required for native USB boards)
    }

    Serial.println("=== TienInterpreter Arduino Example ===");

    // 1. Register platform callbacks
    ti_register_log(arduino_log_callback);
    ti_register_fatal(arduino_fatal_callback);

    // 2. Register native built-in functions
    static param_t delay_params[] = { { VAL_INT, (char *)"ms" } };
    static param_t digital_write_params[] = { { VAL_INT, (char *)"pin" }, { VAL_INT, (char *)"val" } };

    register_builtin_function("print", VAL_VOID, NULL, -1, built_in_print);
    register_builtin_function("delay", VAL_VOID, delay_params, 1, built_in_delay);
    register_builtin_function("digitalWrite", VAL_VOID, digital_write_params, 2, built_in_digital_write);

    // 3. Example Ti script string
    const char *script =
        "int led = 13;\n"
        "int counter = 0;\n"
        "print(\"Starting blink loop in Ti...\\n\");\n"
        "while (counter < 3) {\n"
        "  print(\"Blink #\", counter, \"\\n\");\n"
        "  digitalWrite(led, 1);\n"
        "  delay(500);\n"
        "  digitalWrite(led, 0);\n"
        "  delay(500);\n"
        "  counter = counter + 1;\n"
        "}\n"
        "print(\"Blink loop finished!\\n\");\n";

    // 4. Run the script
    ti_run_string(script);
}

void loop()
{
    // Idle loop
    delay(1000);
}
