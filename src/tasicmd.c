#include "tasicmd.h"


#include <string.h>
#include <stdarg.h>
#include <stdlib.h>


#define TCMD_MIN_TRANSIENT_BUFFER_SIZE 128u




static const char* TCMD_DEFAULT_PROMPT = "(TCMD)> ";

static const char* TCMD_DEFAULT_INTRO  =
"████████╗ █████╗ ███████╗██╗ ██████╗███╗   ███╗██████╗ \n\r"
"╚══██╔══╝██╔══██╗██╔════╝██║██╔════╝████╗ ████║██╔══██╗\n\r"
"   ██║   ███████║███████╗██║██║     ██╔████╔██║██║  ██║\n\r"
"   ██║   ██╔══██║╚════██║██║██║     ██║╚██╔╝██║██║  ██║\n\r"
"   ██║   ██║  ██║███████║██║╚██████╗██║ ╚═╝ ██║██████╔╝\n\r"
"   ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝ ╚═════╝╚═╝     ╚═╝╚═════╝ \n\r"
" Made with ♥ by MDR                                    \n\r";



typedef enum
{
    TCMD_INT8   = 'b', // int8_t*   Signed 8bit integer
    TCMD_UINT8  = 'B', // uint8_t*  Unsigned 8bit integer
    TCMD_INT16  = 'h', // int16_t*  Signed 16bit integer
    TCMD_UINT16 = 'H', // uint16_t* Unsigned 16bit integer
    TCMD_INT32  = 'i', // int32_t*  Signed 32bit integer
    TCMD_UINT32 = 'I', // uint32_t* Unsigned 32bit integer
    
#if TCMD_USE_64BIT_PRECISION
    TCMD_INT64  = 'l', // int64_t*  Signed 64bit integer
    TCMD_UINT64 = 'L', // uint64_t* Unsigned 64bit integer
#endif

#if TCMD_USE_FLOAT
    TCMD_FLOAT  = 'f', // float*    Float 
#endif

#if TCMD_USE_FLOAT && TCMD_USE_64BIT_PRECISION
    TCMD_DOUBLE = 'd', // double*   Double 
#endif

    TCMD_STRING = 's', // char**    String (return the pointer to the token)
    TCMD_BOOL   = 'z', // bool*	    Boolean (0/1, on/off, true/false can be used)
    TCMD_CUSTOM = 'c', //           Custom parser    
} TCMD_FormatSpecifier;



typedef enum
{
    TCMD_KEY_NONE,
    TCMD_KEY_CHAR,
    TCMD_KEY_UP,
    TCMD_KEY_DOWN,
    TCMD_KEY_LEFT,
    TCMD_KEY_RIGHT,
    TCMD_KEY_BACKSPACE,
    TCMD_KEY_ENTER,
    TCMD_KEY_TAB,
} TCMD_KeyEvent;



typedef struct TCMD_CmdEntry
{
    const char* name;
    const char* help;
    const char* usage;
    
    void* userdata;

    TCMD_CmdCallback callback;

    struct TCMD_CmdEntry* next;
} TCMD_CmdEntry;





typedef struct
{
    char lines[TCMD_HISTORY_SIZE][TCMD_LINE_BUFFER_SIZE];
    int head;
    int count;
    int browse;
} TCMD_History;




typedef struct
{
    const char* prompt;
    const char* intro;

    TCMD_CmdIOConfig io;

    uint8_t* workspace;
    size_t workspace_size;
    size_t persistent_offset;


    char line_buffer[TCMD_LINE_BUFFER_SIZE];
    size_t line_pos;
    size_t cursor_pos;


    TCMD_History history;


    TCMD_CustomParser custom_parser;


    TCMD_CmdEntry *command_head;
} TCMD_Module;




#if TCMD_USE_64BIT_PRECISION
    typedef uint64_t tcmd_num_t;
    #define TCMD_NUM_MAX UINT64_MAX
#else
    typedef uint32_t tcmd_num_t;
    #define TCMD_NUM_MAX UINT32_MAX
#endif


static TCMD_Module _tcmd;



static void
_tcmd_write_str(const char* s)
{
    if (s == NULL) return;

    while (*s != '\0')
    {
        _tcmd.io.write(*s++);
    }
}


static void
_tcmd_print_intro(void)
{
    _tcmd_write_str("\n\r");
    _tcmd_write_str(_tcmd.intro);
}


static void
_tcmd_print_prompt(void)
{
    _tcmd_write_str("\n\r");
    _tcmd_write_str(_tcmd.prompt);
}


static void
_tcmd_clear_line_visually(void)
{
    while (_tcmd.line_pos > 0)
    {
        _tcmd_write_str("\b \b");
        _tcmd.line_pos--;
    }

    _tcmd.line_buffer[0] = '\0';
}


static int 
_tcmd_char_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;

    return -1;
}
                                            

static const char* 
_tcmd_parse_prepare(const char* str, int* base)
{
    if (str[0] == '0')
    {
        if (str[1] == 'x' || str[1] == 'X')
        {
            *base = 16;
            
            return str + 2;
        }
        else if (str[1] == 'b' || str[1] == 'B')
        {
            *base = 2;

            return str + 2;
        }
        else if (str[1] >= 0 && str[1] <= 7)
        {
            *base = 8;

            return str + 1;
        }
    }

    *base = 10;

    return str;
}


static TCMD_Result 
_tcmd_str_to_num(const char* str, tcmd_num_t* num)
{
    int base;

    const char* ptr = _tcmd_parse_prepare(str, &base);

    tcmd_num_t result = 0;

    if (*ptr == '\0') return TCMD_ERR_PARSE_EMPTY;

    while (*ptr != '\0')
    {
        int val = _tcmd_char_to_int(*ptr);

        if (val < 0 || val >= base) return TCMD_ERR_PARSE_INVALID_CHAR;

        if (result > (TCMD_NUM_MAX - val) / base)
        {
            return TCMD_ERR_OVERFLOW;
        }

        result = result * base + val;

        ptr++;
    }

    *num = result;

    return TCMD_OK;
}




#define TCMD_IS_NEGATIVE(tok) ({            \
    __typeof__(tok) *_p_tok = &(tok);       \
    bool _negative = false;                 \
    if ((*_p_tok)[0] == '-') {              \
        _negative = true;                   \
        (*_p_tok)++;                        \
    } else if ((*_p_tok)[0] == '+') {       \
        (*_p_tok)++;                        \
    }                                       \
    _negative;                              \
})


#define TCMD_HAS_MINUS(tok) ({                      \
    __typeof__(tok) *_p_tok = &(tok);               \
    TCMD_Result _result = TCMD_OK;                  \
    if ((*_p_tok)[0] == '-') {                      \
        _result = TCMD_ERR_PARSE_NEGATIVE_UNSIGNED; \
    } else if ((*_p_tok)[0] == '+') {               \
        (*_p_tok)++;                                \
    }                                               \
    _result;                                        \
})


#define TCMD_VALIDATE_RANGE(val, max)                       \
    if ((val) > (max)) return TCMD_ERR_PARSE_OUT_OF_RANGE;  \


#define TCMD_GET_NEGATIVE_VALUE(val, t) \
    ((t)((u##t)0 - (u##t)(val)))




static inline TCMD_Result
_tcmd_parse_unsigned_generic(const char** p_token, tcmd_num_t* out, tcmd_num_t max_pos)
{
    TCMD_Result res;

    if ((res = TCMD_HAS_MINUS(*p_token))        != TCMD_OK) return res;
    if ((res = _tcmd_str_to_num(*p_token, out)) != TCMD_OK) return res;

    TCMD_VALIDATE_RANGE(*out, max_pos);

    return TCMD_OK;
}


static TCMD_Result
_tcmd_parse_uint8(const char* token, uint8_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    tcmd_num_t val = 0;

    TCMD_Result result = _tcmd_parse_unsigned_generic(&token, &val, UINT8_MAX);
    
    if (result == TCMD_OK)
    {
        *out = (uint8_t) val;
    }

    return result;
}


static TCMD_Result
_tcmd_parse_uint16(const char* token, uint16_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    tcmd_num_t val = 0;

    TCMD_Result result = _tcmd_parse_unsigned_generic(&token, &val, UINT16_MAX);
    
    if (result == TCMD_OK)
    {
        *out = (uint16_t) val;
    }

    return result;
}


static TCMD_Result
_tcmd_parse_uint32(const char* token, uint32_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    tcmd_num_t val = 0;

    TCMD_Result result = _tcmd_parse_unsigned_generic(&token, &val, UINT32_MAX);
    
    if (result == TCMD_OK)
    {
        *out = (uint32_t) val;
    }

    return result;
}



#if TCMD_USE_64BIT_PRECISION
static TCMD_Result
_tcmd_parse_uint64(const char* token, uint64_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    tcmd_num_t val = 0;

    TCMD_Result result = _tcmd_parse_unsigned_generic(&token, &val, UINT64_MAX);
    
    if (result == TCMD_OK)
    {
        *out = (uint64_t) val;
    }

    return result;
}
#endif




static inline TCMD_Result
_tcmd_parse_signed_generic(const char** p_token, tcmd_num_t* out, bool* is_negative, tcmd_num_t max_pos)
{
    TCMD_Result res;

    *is_negative = TCMD_IS_NEGATIVE(*p_token);

    if ((res = _tcmd_str_to_num(*p_token, out)) != TCMD_OK) return res;

    if (*is_negative)
    {
        TCMD_VALIDATE_RANGE(*out, max_pos + 1);
    }
    else
    {
        TCMD_VALIDATE_RANGE(*out, max_pos);
    }

    return TCMD_OK;
}



static TCMD_Result
_tcmd_parse_int8(const char* token, int8_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;


    bool        is_negative = false;
    tcmd_num_t  val         = 0;
    
    
    TCMD_Result res;

    if ((res = _tcmd_parse_signed_generic(&token, &val, &is_negative, INT8_MAX)) != TCMD_OK) return res;


    if (is_negative)
    {
        *out = TCMD_GET_NEGATIVE_VALUE(val, int8_t);
    }
    else
    {
        *out = (int8_t) val;
    }


    return TCMD_OK;
}


static TCMD_Result
_tcmd_parse_int16(const char* token, int16_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;


    bool        is_negative = false;
    tcmd_num_t  val         = 0;
    
    
    TCMD_Result res;

    if ((res = _tcmd_parse_signed_generic(&token, &val, &is_negative, INT16_MAX)) != TCMD_OK) return res;


    if (is_negative)
    {
        *out = TCMD_GET_NEGATIVE_VALUE(val, int16_t);
    }
    else
    {
        *out = (int16_t) val;
    }


    return TCMD_OK;
}


static TCMD_Result
_tcmd_parse_int32(const char* token, int32_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;


    bool        is_negative = false;
    tcmd_num_t  val         = 0;
    
    
    TCMD_Result res;

    if ((res = _tcmd_parse_signed_generic(&token, &val, &is_negative, INT32_MAX)) != TCMD_OK) return res;


    if (is_negative)
    {
        *out = TCMD_GET_NEGATIVE_VALUE(val, int32_t);
    }
    else
    {
        *out = (int32_t) val;
    }


    return TCMD_OK;
}

#if TCMD_USE_64BIT_PRECISION
static TCMD_Result
_tcmd_parse_int64(const char* token, int64_t* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    bool        is_negative = false;
    tcmd_num_t  val         = 0;
    TCMD_Result res;

    if ((res = _tcmd_parse_signed_generic(&token, &val, &is_negative, INT64_MAX)) != TCMD_OK) return res;

    if (is_negative)
    {
        *out = TCMD_GET_NEGATIVE_VALUE(val, int64_t);
    }
    else
    {
        *out = (int64_t)val;
    }

    return TCMD_OK;
}
#endif


#if TCMD_USE_FLOAT
static TCMD_Result
_tcmd_parse_float(const char* token, float* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    char* endptr;

    *out = strtof(token, &endptr);

    if (endptr == token || *endptr == '\0') return TCMD_ERR_PARSE_INVALID_CHAR;

    return TCMD_OK;
}
#endif


#if TCMD_USE_FLOAT && TCMD_USE_64BIT_PRECISION
static TCMD_Result
_tcmd_parse_double(const char* token, double* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    char* endptr;

    *out = strtod(token, &endptr);

    if (endptr == token || *endptr == '\0') return TCMD_ERR_PARSE_INVALID_CHAR;

    return TCMD_OK;
}
#endif


static TCMD_Result
_tcmd_parse_bool(const char* token, bool* out)
{
    if (token == NULL || out == NULL) return TCMD_ERR_BAD_ARGS;

    if (token[0] == '0' && token[1] == '\0') { *out = false; return TCMD_OK; }
    if (token[0] == '1' && token[1] == '\0') { *out =  true; return TCMD_OK; }

    if (
        (strcmp(token, "true") == 0) || 
        (strcmp(token, "TRUE") == 0) || 
        (strcmp(token,   "on") == 0) || 
        (strcmp(token,   "ON") == 0)
    )
    {
        *out = true;
        return TCMD_OK;
    }

    if (
        (strcmp(token, "false") == 0) || 
        (strcmp(token, "FALSE") == 0) || 
        (strcmp(token,   "off") == 0) ||
        (strcmp(token,   "OFF") == 0)
    )
    {
        *out = false;
        return TCMD_OK;
    }

    return TCMD_ERR_PARSE_INVALID_CHAR;
}




static TCMD_Result
_tcmd_tokenizer(char* str, char** argv, int max_args, int* argc_out)
{
    if (str == NULL || argv == NULL || argc_out == NULL) return TCMD_ERR_TOKENIZER_EMPTY_STRING;

    int argc = 0;

    char* ptr = str;

    while (*ptr != '\0')
    {
        if (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')
        {
            ptr++;
        }

        if (*ptr == '\0') break;

        if (argc > max_args) return TCMD_ERR_TOKENIZER_TOO_MANY_ARGS;

        argv[argc++] = ptr;

        while (*ptr != '\0' && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != '\r')
        {
            ptr++;
        }

        if (*ptr != '\0')
        {
            *ptr = '\0';
            ptr++;
        }
    }

    *argc_out = argc;

    return TCMD_OK;
}





static void
_tcmd_history_save(const char* line, size_t len)
{
    if (line[0] == '\0') return;

    strncpy(_tcmd.history.lines[_tcmd.history.head], line, len);

    _tcmd.history.head = (_tcmd.history.head + 1) % TCMD_HISTORY_SIZE;

    if (_tcmd.history.count < TCMD_HISTORY_SIZE)
    {
        _tcmd.history.count++;
    }

    _tcmd.history.browse = -1;
}



static void
_tcmd_history_reset_browse(void)
{
    _tcmd.history.browse = -1;
}


static void
_tcmd_render_history_line(const char* line)
{
    _tcmd_clear_line_visually();


    strncpy(_tcmd.line_buffer, line, TCMD_LINE_BUFFER_SIZE - 1);
    _tcmd.line_pos = strlen(_tcmd.line_buffer);
    _tcmd.cursor_pos = _tcmd.line_pos;

    _tcmd_write_str(_tcmd.line_buffer);
}


static void
_tcmd_history_recall(bool up)
{
    if (_tcmd.history.count == 0) return;

    
    if (up)
    {
        if (_tcmd.history.browse < (_tcmd.history.count - 1))
        {
            _tcmd.history.browse++;
        }
    }
    else
    {
        if (_tcmd.history.browse > 0) {
            _tcmd.history.browse--;
        } else {
            _tcmd.history.browse = -1;
            _tcmd_clear_line_visually();
            _tcmd.line_buffer[0] = '\0';
            _tcmd.line_pos = 0;
            _tcmd.cursor_pos = 0;
            return;
        }
    }

    int index = (_tcmd.history.head - 1 - _tcmd.history.browse + TCMD_HISTORY_SIZE) % TCMD_HISTORY_SIZE;


    _tcmd_render_history_line(_tcmd.history.lines[index]);
}


static void
_tcmd_execute_line(void)
{
    char** argv = (char**)(_tcmd.workspace + _tcmd.persistent_offset);
    
    int argc = 0;


    TCMD_Result res;

    if ((res = _tcmd_tokenizer(_tcmd.line_buffer, argv, TCMD_MAX_ARGS, &argc)) != TCMD_OK) return;

    if (argc == 0) return;

    TCMD_CmdEntry* entry = _tcmd.command_head;

    bool found = false;

    while (entry != NULL)
    {
        if (strcmp(argv[0], entry->name) == 0)
        {
            entry->callback(argc, argv, entry->userdata);

            found = true;
            break;
        }

        entry = entry->next;
    }

    if (found == false)
    {
        tcmd_default();

        return;
    }
}


TCMD_KeyEvent
_tcmd_process_input(char c, char* out_char)
{
    static enum { STATE_IDLE, STATE_ESC, STATE_BRACKET } state = STATE_IDLE;

    switch(state)
    {
    case STATE_IDLE:
    {
        if (c == 0x1B) { state = STATE_ESC; return TCMD_KEY_NONE; }
        if (c == 0x08 || c == 0x7F) return TCMD_KEY_BACKSPACE;
        if (c == '\r' || c == '\n') return TCMD_KEY_ENTER;
        if (c == '\t') return TCMD_KEY_TAB;

        *out_char = c;

        return TCMD_KEY_CHAR;
    } break;

    case STATE_ESC:
    {
        state = (c == '[') ? STATE_BRACKET : STATE_IDLE;

        return TCMD_KEY_NONE;
    } break;

    case STATE_BRACKET:
    {
        state = STATE_IDLE;

        if (c == 'A') return TCMD_KEY_UP;
        if (c == 'B') return TCMD_KEY_DOWN;
        if (c == 'C') return TCMD_KEY_RIGHT;
        if (c == 'D') return TCMD_KEY_LEFT;

        return TCMD_KEY_NONE;
    } break;

    default:
    {
        state = STATE_IDLE;
        return TCMD_KEY_NONE;
    }break;
    }

}



static void
_tcmd_handle_char(char c)
{
    if (_tcmd.line_pos < TCMD_LINE_BUFFER_SIZE - 1)
    {
        _tcmd.line_buffer[_tcmd.line_pos++] = c;

        _tcmd.cursor_pos++;

    #if TCMD_ENABLE_COMMAND_ECHO
        _tcmd.io.write(c); /* Echo */
    #endif
    }
}


static void
_tcmd_handle_backspace(void)
{
    if (_tcmd.line_pos > 0)
    {
        _tcmd.line_buffer[--_tcmd.line_pos] = '\0';
        _tcmd.cursor_pos--;
        _tcmd.io.write(0x08); _tcmd.io.write(' '); _tcmd.io.write(0x08);
    }
}


static void
_tcmd_handle_execute(void)
{
    if (_tcmd.line_pos > 0)
    {
        // Save linebuffer into the history
        _tcmd_history_save(_tcmd.line_buffer, _tcmd.line_pos);

        // Send CRLF
        _tcmd.io.write('\r');
        _tcmd.io.write('\n');
    
        // Call the pre execute
        tcmd_pre_execute();
    
        // Execute
        _tcmd_execute_line();
    
        // Call the post execute
        tcmd_post_execute();
    
        // Print the prompt
        _tcmd_print_prompt();
    }

    memset(&_tcmd.line_buffer[0], 0, _tcmd.line_pos);
    
    _tcmd.line_pos = 0;

    _tcmd_history_reset_browse();
}



static void
_tcmd_handle_left(void)
{
    if (_tcmd.cursor_pos > 0)
    {
        _tcmd.cursor_pos--;
        _tcmd_write_str("\b");
    }
}

static void
_tcmd_handle_right(void)
{
    if (_tcmd.cursor_pos < _tcmd.line_pos)
    {
        _tcmd.cursor_pos++;
        _tcmd_write_str("\x1b[C");
    }
}



TCMD_Result 
tcmd_init (const TCMD_ModuleConfig* config)
{
    if ((config == NULL)                || 
        (config->workspace == NULL )    || 
        (config->workspace_size == 0)   || 
        (config->io == NULL)            ||
        (config->io->read == NULL)      ||
        (config->io->write == NULL)
    )
    {
        return TCMD_ERR_BAD_ARGS;
    }

    memset(&_tcmd, 0, sizeof(_tcmd));


    _tcmd.prompt = (config->prompt) ? config->prompt : TCMD_DEFAULT_PROMPT;
    _tcmd.intro  = (config->intro ) ? config->intro  : TCMD_DEFAULT_INTRO;


    _tcmd.io.read   = config->io->read;
    _tcmd.io.write  = config->io->write;
    

    _tcmd.workspace          = config->workspace;
    _tcmd.workspace_size     = config->workspace_size;
    _tcmd.persistent_offset  = 0;

    _tcmd.line_pos   = 0;
    _tcmd.cursor_pos = 0;


    _tcmd.command_head = NULL;


    _tcmd_print_intro();
    _tcmd_print_prompt();

    return TCMD_OK;
}





TCMD_Result
tcmd_register_command (
    const char* name,
    const char* help,
    const char* usage,
    TCMD_CmdCallback callback,
    void* userdata
)
{
    if ((name == NULL) || (help == NULL) || (usage == NULL) || (callback == NULL))
    {
        return TCMD_ERR_BAD_ARGS;
    }


    size_t entry_size       = sizeof(TCMD_CmdEntry);
    const size_t alignament = sizeof(uintptr_t);


    size_t aligned_offset = (_tcmd.persistent_offset + (alignament - 1)) & ~(alignament - 1);


    if ((aligned_offset + entry_size + TCMD_MIN_TRANSIENT_BUFFER_SIZE) > _tcmd.workspace_size)
    {
        return TCMD_ERR_NOT_ENOUGH_SPACE;
    }


    TCMD_CmdEntry *new_cmd = (TCMD_CmdEntry *) (_tcmd.workspace + aligned_offset);


    new_cmd->name       = name;
    new_cmd->help       = help;
    new_cmd->usage      = usage;
    new_cmd->callback   = callback;
    new_cmd->userdata   = userdata;


    new_cmd->next       = _tcmd.command_head;
    _tcmd.command_head  = new_cmd;


    _tcmd.persistent_offset += aligned_offset + entry_size;


    return TCMD_OK;
}




TCMD_Result 
tcmd_set_custom_parser(TCMD_CustomParser parser)
{
    if (parser == NULL)
    {
        return TCMD_ERR_BAD_ARGS;
    }

    _tcmd.custom_parser = parser;

    return TCMD_OK;
}




TCMD_Result 
tcmd_unpack(int argc, char** argv, char* fmt, ...)
{
    if (argc <= 0 || argv == NULL || fmt == NULL) return TCMD_ERR_BAD_ARGS;

    TCMD_Result result = TCMD_OK;

    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; ++i)
    {
        // Skipping the command
        int arg_idx = i + 1;

        if (arg_idx >= argc)
        {
            result = TCMD_ERR_FEW_ARGS;
            break;
        }

        char* token = argv[arg_idx];

        switch(fmt[i])
        {
        case TCMD_INT8:
        {
            result = _tcmd_parse_int8(token, va_arg(args, int8_t*));
        } break;

        case TCMD_UINT8:
        {
            result = _tcmd_parse_uint8(token, va_arg(args, uint8_t*));
        } break;

        case TCMD_INT16:
        {
            result = _tcmd_parse_int16(token, va_arg(args, int16_t*));
        } break;

        case TCMD_UINT16:
        {
            result = _tcmd_parse_uint16(token, va_arg(args, uint16_t*)); 
        } break;

        case TCMD_INT32:
        {
            result = _tcmd_parse_int32(token, va_arg(args, int32_t*));
        } break;

        case TCMD_UINT32:
        {
            result = _tcmd_parse_uint32(token, va_arg(args, uint32_t*));
        } break;

#if TCMD_USE_64BIT_PRECISION
        case TCMD_INT64:
        {
            result = _tcmd_parse_int64(token, va_arg(args, int64_t*));
        } break;

        case TCMD_UINT64:
        {
            result = _tcmd_parse_uint64(token, va_arg(args, uint64_t*));
        } break;
#endif

#if TCMD_USE_FLOAT
        case TCMD_FLOAT:
        {
            result = _tcmd_parse_float(token, va_arg(args, float*));
        } break;
#endif

#if TCMD_USE_FLOAT && TCMD_USE_64BIT_PRECISION
        case TCMD_DOUBLE:
        {
            result = _tcmd_parse_double(token, va_arg(args, double*));
        } break;
#endif
        case TCMD_STRING:
        {
            *(va_arg(args, char**)) = token;
        } break;

        case TCMD_BOOL:
        {
            result = _tcmd_parse_bool(token, va_arg(args, bool*));
        } break;

        case TCMD_CUSTOM:
        {
            if (_tcmd.custom_parser)
            {
                result = _tcmd.custom_parser(token, va_arg(args, void*));
            }
            else
            {
                result = TCMD_ERR_PARSE_CUSTOM_PARSER_IS_ABSENT;
            }
        } break;

        default:
        {
            result = TCMD_ERR_INVALID_FORMAT;
        } break;
        }

        if (result != TCMD_OK) break;
    }


    va_end(args);

    return result;
}



void 
tcmd_run(void)
{
    char raw_c;
    char processed_c;

    if (_tcmd.io.read(&raw_c) == false) return;


    TCMD_KeyEvent key_evt = _tcmd_process_input(raw_c, &processed_c);

    switch(key_evt)
    {
        case TCMD_KEY_CHAR:
        {
            _tcmd_handle_char(processed_c);
        } break;

        case TCMD_KEY_BACKSPACE:
        {
            _tcmd_handle_backspace();
        } break;

        case TCMD_KEY_ENTER:
        {
            _tcmd_handle_execute();
        } break;

        case TCMD_KEY_UP:
        {
            _tcmd_history_recall(true);
        } break;

        case TCMD_KEY_DOWN:
        {
            _tcmd_history_recall(false);
        } break;

        case TCMD_KEY_LEFT:
        {
            _tcmd_handle_left();
        } break;

        case TCMD_KEY_RIGHT:
        {
            _tcmd_handle_right();
        } break;

        case TCMD_KEY_TAB:
        default:
        {
            //
        } break;

    }
}




__attribute__((weak)) void 
tcmd_default(void)
{
    _tcmd_write_str("Command not found!");
}




__attribute__((weak)) void 
tcmd_pre_execute(void)
{
    return;
}



__attribute__((weak)) void 
tcmd_post_execute(void)
{
    return;
}


