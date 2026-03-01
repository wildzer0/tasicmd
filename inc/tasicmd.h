#ifndef TASICMD_H_
#define TASICMD_H_

#include "tasicmd_configuration.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>



typedef enum
{
    TCMD_OK,
    TCMD_ERR,
    TCMD_ERR_BAD_ARGS,
    TCMD_ERR_FEW_ARGS,
    TCMD_ERR_NOT_ENOUGH_SPACE,
    TCMD_ERR_INVALID_FORMAT,
    TCMD_ERR_PARSE_EMPTY,
    TCMD_ERR_PARSE_INVALID_CHAR,
    TCMD_ERR_PARSE_OUT_OF_RANGE,
    TCMD_ERR_PARSE_NEGATIVE_UNSIGNED,
    TCMD_ERR_PARSE_CUSTOM_PARSER_IS_ABSENT,
    TCMD_ERR_OVERFLOW
} TCMD_Result;


typedef struct 
{
    bool (*read)(char* c);
    void (*write)(char c);
} TCMD_CmdIOConfig;


typedef struct
{
    const char* prompt;
    const char* intro;

    uint8_t* workspace;
    size_t   workspace_size;

    const TCMD_CmdIOConfig* io;
} TCMD_ModuleConfig;



typedef void (*TCMD_CmdCallback)(int argc, char** argv, void* userdata);
typedef TCMD_Result (*TCMD_CustomParser)(const char* token, void* out);


/**
 * @brief Initializes the command module and sets up the workspace.
 *        This function clears the internal global state, configures the I/O hooks,
 *        and partitions the provided workspace for command storage and parsing.
 * 
 * @param config         The module configuration info.
 * 
 * @return TCMD_Result   TCMD_OK on success, TCMD_ERR_BAD_ARGS if inputs are invalid.
 * 
 * @note This library is implemented as a singleton; subsequent calls to this 
 *       function will reset the entire module and clear all registered commands.
 */
TCMD_Result tcmd_init(const TCMD_ModuleConfig* config);


/**
 * @brief Registers a new command entry into the persistent workspace.
 *        This function calculates the required space, ensures memory alignment,
 *        and appends the command to the internal linked list.
 * 
 * @param name      Command string (e.g., "status").
 * @param help      Brief description for the help command.
 * @param usage     Parameter usage guide.
 * @param callback  Function pointer to the command logic.
 * @param userdata  Optional pointer to user-defined data.
 * 
 * @return TCMD_Result TCMD_OK on success, or error code.
 */
TCMD_Result tcmd_register_command(const char* name, const char* help, const char* usage, TCMD_CmdCallback callback, void* userdata);


/**
 * @brief Registers the global custom parser for user-defined types.
 *        This parser is invoked by tcmd_unpack() whenever the 'c' format 
 *        specifier is encountered in the format string.
 * 
 * @param parser Function pointer matching the TCMD_CustomParser signature.
 * 
 * @return TCMD_OK if registered successfully, TCMD_ERR_BAD_ARGS if the 
 * provided pointer is NULL.
 * 
 * @note This function must be called after tcmd_init() and before any command
 *       execution that requires custom type unpacking. It is recommended to
 *       perform this registration during the system's setup phase.
 */
TCMD_Result tcmd_set_custom_parser(TCMD_CustomParser parser);


/**
 * @brief Unpacks command arguments into typed variables using a format string.
 * 
 * @param argc Number of available arguments from the command callback.
 * @param argv Array of argument strings.
 * @param fmt  Format string (e.g., "BHi" for u8, u16, i32).
 * @param ...  Pointers to the destination variables.
 * 
 * @return TCMD_Result TCMD_OK on success, or an error if parsing/validation fails.
 */
TCMD_Result tcmd_unpack(int argc, char** argv, char* fmt, ...);


#warning MDR: To be implemented
void tcmd_run(void);



// Hooks
void tcmd_on_unknown_command(void);
void tcmd_pre_execute(void);
void tcmd_post_execute(void);




#endif // TASICMD_H_
