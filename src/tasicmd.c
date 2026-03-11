#include "tasicmd.h"


#include <string.h>
#include <stdarg.h>
#include <stdlib.h>


#define TCMD_MIN_TRANSIENT_BUFFER_SIZE 128u
#define TCMD_NAME_COLUMN_WIDTH 16



#define SAVE_CURSOR_POS     _tcmd_write_str("\x1b[s")
#define CLEAR_LINE          _tcmd_write_str("\x1b[K")
#define RESTORE_CURSOR_POS  _tcmd_write_str("\x1b[u")
#define MOVE_CURSOR_RIGHT   _tcmd_write_str("\x1b[C")
#define CLEAR_SCREEN        _tcmd_write_str("\x1b[2J")
#define MOVE_CURSOR_UPLEFT  _tcmd_write_str("\x1b[H")
#define MOVE_CURSOR_LEFT    _tcmd_write_str("\b")
#define CRLF                _tcmd_write_str("\r\n")
#define CR                  _tcmd_write_str("\r")
#define WRITE_PROMPT        _tcmd_write_str(_tcmd.prompt)
#define WRITE_BUFFER        _tcmd_write_str(_tcmd.line_buffer)



static const char* TCMD_DEFAULT_PROMPT = "(TCMD)> ";

static const char* TCMD_DEFAULT_INTRO  =
"              TASICMD - Embedded CLI Library           \n\r"
"                  Made with <3 by MDR                  \n\r";



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
    TCMD_SKIP   = '_', //           Skip the current token
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
    TCMD_KEY_DELETE,
    TCMD_KEY_CLEAR_LINE,
    TCMD_KEY_HOME,
    TCMD_KEY_END,
    TCMD_KEY_BACKGROUND,
    TCMD_KEY_FOREGROUND,
    TCMD_KEY_CLEAR,
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

    bool is_visible;


    TCMD_History history;


    TCMD_CustomParser custom_parser;


    TCMD_CmdEntry *command_head;

    size_t command_num;
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
    CRLF;

    _tcmd_write_str(_tcmd.intro);
}


static void
_tcmd_print_prompt(void)
{
    CRLF;

    WRITE_PROMPT;
}


static void
_tcmd_clear_line_visually(void)
{
    CR;
    WRITE_PROMPT;
    
    CLEAR_LINE;

    _tcmd.line_pos = 0;
    _tcmd.cursor_pos = 0;
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
        #warning The octal parsing recognize a single 0 as octal number
        // else if (str[1] >= 0 && str[1] <= 7)
        // {
        //     *base = 8;

        //     return str + 1;
        // }
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




static void
_tcmd_normalize_string(char* str)
{
    int src = 0;
    int dst = 0;
    int len = strlen(str);

    while (src < len && str[src] == ' ') src++;

    while (src < len)
    {
        if (str[src] != ' ') 
        {
            str[dst++] = str[src++];
        }
        else
        {
            if (dst > 0 && str[src + 1] != ' ' && str[src + 1] != '\0')
            {
                str[dst++] = ' ';
            }

            src++;
        }
    }

    str[dst] = '\0';
}





static TCMD_Result
_tcmd_tokenizer(char* str, char** argv, int max_args, int* argc_out)
{
    if (str == NULL || argv == NULL || argc_out == NULL) return TCMD_ERR_TOKENIZER_EMPTY_STRING;

    int     argc = 0;
    char*   ptr  = str;

    while (*ptr != '\0')
    {
        if (*ptr == '\0') break;

        if (argc > max_args) return TCMD_ERR_TOKENIZER_TOO_MANY_ARGS;

        if (*ptr == '"')
        {
            ptr++;
            argv[argc++] = ptr;

            while (*ptr != '"') 
            {
                if (*ptr != '\0')   ptr++;
                else return TCMD_ERR_TOKENIZER_STRING_NOT_CLOSED;
            }
        }
        else
        {
            argv[argc++] = ptr;
            while (*ptr != '\0' && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != '\r') ptr++;
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

    WRITE_BUFFER;
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





#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MIN3(a, b, c)   MIN(MIN((a), (b)), (c))

static int
_tcmd_osa_fast(const char* s1, const char* s2, int threshold)
{
    int n = strlen(s1);
    int m = strlen(s2);

    int len_diff = n > m ? n - m : m - n;
    if (len_diff > threshold) return threshold + 1;

    int row0[TCMD_LINE_BUFFER_SIZE + 1];
    int row1[TCMD_LINE_BUFFER_SIZE + 1];
    int row2[TCMD_LINE_BUFFER_SIZE + 1];

    int *prev2 = row0;
    int *prev1 = row1;
    int *curr  = row2;

    for (int j = 0; j <= m; j++) prev1[j] = j;

    for (int i = 1; i <= n; i++)
    {
        curr[0] = i;
        int current_row_min = i;

        for (int j = 1; j <= m; j++)
        {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            
            curr[j] = MIN3(prev1[j] + 1, curr[j - 1] + 1, prev1[j - 1] + cost);

            if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1])
            {
                curr[j] = MIN(curr[j], prev2[j - 2] + 1);
            }

            if (curr[j] < current_row_min) current_row_min = curr[j];
        }

        if (current_row_min > threshold) return threshold + 1;

        int *tmp = prev2;
        prev2 = prev1;
        prev1 = curr;
        curr = tmp;
    }

    return prev1[m];
}



static void
_tcmd_find_best_match(const char* wrong_cmd)
{
    TCMD_CmdEntry* curr = _tcmd.command_head;
        
    int         min_distance    = 3;
    const char* best_match      = NULL;

    while (curr)
    {
        int distance = _tcmd_osa_fast(wrong_cmd, curr->name, min_distance);

        if (distance < min_distance)
        {
            min_distance = distance;
            best_match = curr->name;

            if (min_distance == 1) break;   
        }

        curr = curr->next;
    }

    if (best_match == NULL)
    {
        _tcmd_write_str("The command ");
        _tcmd_write_str(wrong_cmd);
        _tcmd_write_str(" is unknown.\n\r");
    }
    else
    {
        _tcmd_write_str("Did you mean this? [");
        _tcmd_write_str(best_match);
        _tcmd_write_str("]\r\n");
    }
}




static void
_tcmd_execute_line(void)
{
    char** argv = (char**)(_tcmd.workspace + _tcmd.persistent_offset);
    
    int argc = 0;

    _tcmd_normalize_string(_tcmd.line_buffer);

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
        _tcmd_find_best_match(argv[0]);
    }
}



TCMD_KeyEvent
_tcmd_process_input(char c, char* out_char)
{
    static enum { STATE_IDLE, STATE_ESC, STATE_BRACKET, STATE_DELETE } state = STATE_IDLE;

    switch(state)
    {
    case STATE_IDLE:
    {
        if (c == 0x1B) { state = STATE_ESC; return TCMD_KEY_NONE; }
        if (c == 0x08 || c == 0x7F) return TCMD_KEY_BACKSPACE;
        if (c == '\r' || c == '\n') return TCMD_KEY_ENTER;
        if (c == '\t') return TCMD_KEY_TAB;
        if (c == 0x15) return TCMD_KEY_CLEAR_LINE;
        if (c == 0x01) return TCMD_KEY_HOME;
        if (c == 0x05) return TCMD_KEY_END;
        if (c == 0x02) return TCMD_KEY_BACKGROUND;
        if (c == 0x06) return TCMD_KEY_FOREGROUND;
        if (c == 0x0C) return TCMD_KEY_CLEAR;

        // Allowing only characters from space to ~
        if (c >= 0x20 && c <= 0x7E)
        {
            *out_char = c;
            
            return TCMD_KEY_CHAR;
        }

        return TCMD_KEY_NONE;
    } break;

    case STATE_ESC:
    {
        state = (c == '[') ? STATE_BRACKET : STATE_IDLE;

        return TCMD_KEY_NONE;
    } break;

    case STATE_BRACKET:
    {
        state = STATE_IDLE;

        if (c == 'A') { state = STATE_IDLE;     return TCMD_KEY_UP;     }
        if (c == 'B') { state = STATE_IDLE;     return TCMD_KEY_DOWN;   }
        if (c == 'C') { state = STATE_IDLE;     return TCMD_KEY_RIGHT;  }
        if (c == 'D') { state = STATE_IDLE;     return TCMD_KEY_LEFT;   }
        if (c == '3') { state = STATE_DELETE;   return TCMD_KEY_NONE;   }

        state = STATE_IDLE;

        return TCMD_KEY_NONE;
    } break;

    case STATE_DELETE:
    {
        state = STATE_IDLE;
        
        if (c == '~') return TCMD_KEY_DELETE;

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
    if (_tcmd.line_pos >= TCMD_LINE_BUFFER_SIZE - 1) return;

    if (_tcmd.cursor_pos < _tcmd.line_pos)
    {
        int tail = _tcmd.line_pos - _tcmd.cursor_pos;

        memmove(
            &_tcmd.line_buffer[_tcmd.cursor_pos + 1],
            &_tcmd.line_buffer[_tcmd.cursor_pos],
            tail
        );


        _tcmd.line_buffer[_tcmd.cursor_pos] = c;

        _tcmd.line_pos++;
        _tcmd.cursor_pos++;

        _tcmd_write_str(&_tcmd.line_buffer[_tcmd.cursor_pos - 1]);

        for (int i = 0; i < tail; ++i) MOVE_CURSOR_LEFT;
    }
    else
    {
        _tcmd.line_buffer[_tcmd.line_pos] = c;

        _tcmd.line_pos++;
        _tcmd.cursor_pos++;

        _tcmd.io.write(c);
    }
}


static void
_tcmd_handle_backspace(void)
{
    if (_tcmd.cursor_pos == 0) return;

    if (_tcmd.cursor_pos < _tcmd.line_pos)
    {
        int tail = _tcmd.line_pos - _tcmd.cursor_pos;

        memmove(
            &_tcmd.line_buffer[_tcmd.cursor_pos - 1],
            &_tcmd.line_buffer[_tcmd.cursor_pos],
            tail
        );


        _tcmd.line_pos--;
        _tcmd.cursor_pos--;

        _tcmd.line_buffer[_tcmd.line_pos] = '\0';

        MOVE_CURSOR_LEFT;
        SAVE_CURSOR_POS;
        CLEAR_LINE;

        _tcmd_write_str(&_tcmd.line_buffer[_tcmd.cursor_pos]);

        RESTORE_CURSOR_POS;
    }
    else
    {
        _tcmd.line_pos--;
        _tcmd.cursor_pos--;
        _tcmd.line_buffer[_tcmd.line_pos] = '\0';
        _tcmd_write_str("\b \b");
    }
}


static void
_tcmd_handle_execute(void)
{
    if (_tcmd.line_pos > 0)
    {
        _tcmd.line_buffer[_tcmd.line_pos] = '\0';

        // Save raw linebuffer into the history
        _tcmd_history_save(_tcmd.line_buffer, _tcmd.line_pos);

        // Send CRLF
        CRLF;
    
        // Call the pre execute hook
        tcmd_pre_execute();
    
        // Execute the commnad
        _tcmd_execute_line();
    
        // Call the post execute hook
        tcmd_post_execute();
    
        // Re-print the prompt
        _tcmd_print_prompt();
    }

    memset(&_tcmd.line_buffer, 0, sizeof(_tcmd.line_buffer));

    _tcmd.line_pos = 0;
    _tcmd.cursor_pos = 0;


    _tcmd_history_reset_browse();
}



static void
_tcmd_handle_left(void)
{
    if (_tcmd.cursor_pos > 0)
    {
        _tcmd.cursor_pos--;
        MOVE_CURSOR_LEFT;
    }
}


static void
_tcmd_handle_right(void)
{
    if (_tcmd.cursor_pos < _tcmd.line_pos)
    {
        _tcmd.cursor_pos++;
        MOVE_CURSOR_RIGHT;
    }
}



static void
_tcmd_handle_delete(void)
{
    if (_tcmd.cursor_pos >= _tcmd.line_pos) return;

    int tail = _tcmd.line_pos - _tcmd.cursor_pos - 1;

    if (tail > 0)
    {
        memmove(
            &_tcmd.line_buffer[_tcmd.cursor_pos],
            &_tcmd.line_buffer[_tcmd.cursor_pos + 1],
            tail
        );
    }

    _tcmd.line_pos--;
    _tcmd.line_buffer[_tcmd.line_pos] = '\0';

    SAVE_CURSOR_POS;
    CLEAR_LINE;

    if (tail > 0)
    {
        _tcmd_write_str(&_tcmd.line_buffer[_tcmd.cursor_pos]);
    }

    RESTORE_CURSOR_POS;
}



static void
_tcmd_handle_clear_line(void)
{
    CR;
    WRITE_PROMPT;
    CLEAR_LINE;

    _tcmd.line_pos = 0;
    _tcmd.cursor_pos = 0;
    _tcmd.line_buffer[0] = '\0';
}



static void
_tcmd_handle_home(void)
{
    if (_tcmd.cursor_pos > 0)
    {
        _tcmd.cursor_pos = 0;
        CR;
        WRITE_PROMPT;
    }
}


static void
_tcmd_handle_end(void)
{
    if (_tcmd.cursor_pos < _tcmd.line_pos)
    {
        _tcmd_write_str(&_tcmd.line_buffer[_tcmd.cursor_pos]);
        _tcmd.cursor_pos = _tcmd.line_pos;
    }
}



static size_t
_tcmd_count_common_prefix(const char* s1, const char* s2)
{
    size_t count = 0;

    while (s1[count] && s2[count] && (s1[count] == s2[count]))
    {
        count++;
    }

    return count;
}



static void
_tcmd_autocomplete(const char* reference, size_t from, size_t to, size_t size, bool add_space)
{
    strncpy(&_tcmd.line_buffer[from], &reference[from], to);

    if (add_space)
    {
        _tcmd.line_buffer[size++] = ' ';
    }

    _tcmd.line_buffer[size] = '\0';

    _tcmd.line_pos += add_space ? to + 1 : to;
    _tcmd.cursor_pos = _tcmd.line_pos;
}



static void
_tcmd_handle_tab(void)
{
    if (strchr(_tcmd.line_buffer, ' ') != NULL || _tcmd.line_buffer[_tcmd.cursor_pos + 1] != '\0') return;


    const char* matches[_tcmd.command_num];

    size_t match_count  = 0;
    size_t input_len    = _tcmd.cursor_pos;
    size_t lcp          = 0;
    bool   first_match  = false;

    TCMD_CmdEntry* curr = _tcmd.command_head;

    while (curr)
    {
        if (strncmp(_tcmd.line_buffer, curr->name, input_len) == 0)
        {
            matches[match_count] = curr->name;

            if (first_match == false)
            {
                first_match = true;

                lcp = strlen(curr->name);
            }
            else
            {
                size_t common = _tcmd_count_common_prefix(matches[match_count - 1], curr->name);
                
                if (common < lcp) lcp = common;
            }

            match_count++;
        }


        curr = curr->next;
    }

    if (match_count == 0)
    {
        return;
    }
    else if (match_count == 1)
    {
        const char* the_reference = matches[0];

        size_t rest = lcp - input_len;

        _tcmd_autocomplete(the_reference, input_len, rest, lcp, true);

        _tcmd_write_str(&_tcmd.line_buffer[input_len]);
    }
    else
    {
        CRLF;

        for (size_t i = 0; i < match_count; ++i)
        {
            _tcmd_write_str(matches[i]);
            _tcmd_write_str(" ");
        }

        CRLF;
        CRLF;

        WRITE_PROMPT;

        const char* the_reference = matches[0];

        size_t rest = lcp - input_len;

        _tcmd_autocomplete(the_reference, input_len, rest, lcp, false);

        WRITE_BUFFER;
    }
}



static void
_tcmd_handle_background(void)
{
    if (_tcmd.is_visible == true)
    {
        _tcmd.is_visible = false;

        CR;
        CLEAR_LINE;
    }
}



static void
_tcmd_handle_foreground(void)
{
    if (_tcmd.is_visible == false)
    {
        _tcmd.is_visible = true;

        WRITE_PROMPT;
        WRITE_BUFFER;
    }
}



static void
_tcmd_handle_clear(void)
{
    CLEAR_SCREEN;
    MOVE_CURSOR_UPLEFT;

    WRITE_PROMPT;
    WRITE_BUFFER;
}


static void
_tcmd_write_spaces(int n)
{
    while(n-- > 0) _tcmd.io.write(' ');
}


static void
_tcmd__help_handler(int argc, char** argv, void* userargs)
{
    (void) userargs;

    if (argc == 1)
    {
        _tcmd_write_str("Commands:\n\r");
        
        TCMD_CmdEntry* curr = _tcmd.command_head;

        while(curr)
        {
            int name_len = strlen(curr->name);
            int padding = TCMD_NAME_COLUMN_WIDTH - name_len;

            if (padding < 1) padding = 1;

            _tcmd_write_str("  ");
            _tcmd_write_str(curr->name);
            if (curr->usage)
            {
                _tcmd_write_spaces(padding);
                _tcmd_write_str(curr->usage);
            }
            CRLF;
            
            curr = curr->next;
        }
    }
    else
    {
        if (strcmp(argv[1], "help") == 0) return;


        TCMD_CmdEntry* curr = _tcmd.command_head;

        while(curr)
        {
            if (strcmp(argv[1], curr->name) == 0)
            {
                _tcmd_write_str("Info ");
                _tcmd_write_spaces(TCMD_NAME_COLUMN_WIDTH - 5);
                _tcmd_write_str(curr->help ? curr->help : "Command don't have any info.");
                CRLF;

                _tcmd_write_str("Usage ");
                _tcmd_write_spaces(TCMD_NAME_COLUMN_WIDTH - 6);
                _tcmd_write_str(curr->usage ? curr->usage : "Command don't have any usage info.");
                CRLF;
                return;
            }

            curr = curr->next;
        }
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

    _tcmd.is_visible = true;


    _tcmd.command_head = NULL;
    _tcmd.command_num  = 0;

    tcmd_register_command(
        "help", 
        "this help",
        "help [<command>]",
        _tcmd__help_handler,
        NULL
    );


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
    _tcmd.command_num++;


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

        case TCMD_SKIP:
        {
            // Skipt the current token
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

    if (_tcmd.is_visible == false && key_evt != TCMD_KEY_FOREGROUND) return;

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

        case TCMD_KEY_DELETE:
        {
            _tcmd_handle_delete();
        } break;

        case TCMD_KEY_CLEAR_LINE:
        {
            _tcmd_handle_clear_line();
        } break;

        case TCMD_KEY_HOME:
        {
            _tcmd_handle_home();
        } break;

        case TCMD_KEY_END:
        {
            _tcmd_handle_end();
        } break;

        case TCMD_KEY_TAB:
        {
            _tcmd_handle_tab();
        } break;

        case TCMD_KEY_BACKGROUND:
        {
            _tcmd_handle_background();
        } break;

        case TCMD_KEY_FOREGROUND:
        {
            _tcmd_handle_foreground();
        } break;

        case TCMD_KEY_CLEAR:
        {
            _tcmd_handle_clear();
        } break;


        default:
        {
            //
        } break;

    }
}



void 
tcmd_print_usage(const char* name)
{
    if (name == NULL) return;

    TCMD_CmdEntry* curr = _tcmd.command_head;

    while(curr)
    {
        if (strcmp(curr->name, name) == 0)
        {
            _tcmd_write_str("Usage ");
            _tcmd_write_spaces(TCMD_NAME_COLUMN_WIDTH - 6);
            _tcmd_write_str(curr->usage ? curr->usage : "Command has usage info.");

            CRLF;

            return;
        }

        curr = curr->next;
    }
}



void
tcmd_clear_prompt(void)
{
    if (_tcmd.is_visible == true)
    {
        CR;
        CLEAR_LINE;
    }
}



void
tcmd_restore_prompt(void)
{
    if (_tcmd.is_visible == true)
    {
        WRITE_PROMPT;
        WRITE_BUFFER;
    }
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


