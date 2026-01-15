#ifndef GENERIC_CMDLINE_H
#define GENERIC_CMDLINE_H

#include <stdbool.h>
#include <stdint.h>

#define CMD_MAX_LEN        256
#define HISTORY_DEPTH      15
#define MAX_CMDS           128
#define MAX_ARGC           32
#define KEY_ENTER          0x0D
#define KEY_BACKSPACE      0x08
#define KEY_ESC            0x1B
#define KEY_UP             0x41
#define KEY_DOWN           0x42
#define KEY_TAB            0x09
#define KEY_CTRL_C         0x03
#define PROMPT_MAX_LEN     16

typedef int (*cmd_handler)(int argc, char* argv[]);
typedef void (*cmd_completer)(void* cli,const char* prefix, char** matches, int* match_count);

typedef struct {
    char* buffer;
    uint16_t size;
    volatile uint16_t wr;
    volatile uint16_t rd;
    void (*lock)(void);
    void (*unlock)(void);
} cmd_queue_t;

typedef struct {
    const char* name;
    const char* help;
    cmd_handler handler;
} cmd_entry;

typedef struct {
    char buffer[CMD_MAX_LEN+1];
    uint8_t cursor_pos;
    char* history[HISTORY_DEPTH];
    uint8_t history_count;
    int8_t history_idx;
    
    cmd_queue_t* input_queue;
    cmd_entry commands[MAX_CMDS];
    uint8_t cmd_count;
    
    void (*output)(char);
    void (*output_str)(const char*);
    void (*unknown_cmd)(char*);
    cmd_completer completer;
    char* completion_matches[8];
    int completion_index;
    char prompt[PROMPT_MAX_LEN];
} cmdline_t;

/* Initialize queue */
void queue_init(cmd_queue_t* q, char* buf, uint16_t size, void (*lock)(void), void (*unlock)(void));

// Check if queue is empty
bool queue_empty(cmd_queue_t* q);

// Check if queue is full
bool queue_full(cmd_queue_t* q);

/* Thread-safe enqueue */
bool queue_put(cmd_queue_t* q, char c);

/* Thread-safe dequeue */
char queue_get(cmd_queue_t* q);

/* Initialize command line */
void cmdline_init(cmdline_t* cli, cmd_queue_t* q, void (*output)(char), void (*unknown_cmd)(char*), const char* prompt);

/* Register command */
bool cmdline_register(cmdline_t* cli, const char* name, const char* help, cmd_handler handler);

void cmdline_register_output_str(cmdline_t* cli, void (*output_str)(const char*)) ;

/* Process input */
void cmdline_process(cmdline_t* cli);




#endif