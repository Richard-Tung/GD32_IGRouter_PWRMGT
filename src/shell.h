/*
    Copyright 2026 Richard Tung <dyq@dyq.me>
*/

#include <Arduino.h>
#include <strings.h>

#define SHELL_MAX_COMMAND_LENGTH 64
#define SHELL_MAX_CMD_PER_INTP 16
#define SHELL_MAX_SUB_INTP_PER_INTP 8
#define SHELL_MAX_HELP_LENGTH SHELL_MAX_COMMAND_LENGTH+32
#define SHELL_PROMPT "> "
#define SHELL_NEW_LINE "\r\n"
#define SHELL_SCREEN_WIDTH 80

class Shell;

void trimstr(char* str);
void splitArgs(char* str, char* cmd, char* args, bool* cmd_has_tail_space=nullptr);

class CommandInterpreter
{

    public:
    typedef void (*CommandHandle)(Shell& shell, char* args);

    struct CommandItem {
        const char* cmd;
        const char* description;
        CommandHandle handle;
    };

    private:
    CommandItem cmdlist[SHELL_MAX_CMD_PER_INTP];
    CommandInterpreter* iplist[SHELL_MAX_SUB_INTP_PER_INTP];
    uint8_t cmd_count=0;
    uint8_t ip_count=0;
    Shell& shell;

    public:
    const char* cmd;
    const char* description;
    CommandInterpreter(Shell& s, const char* cmd, const char* desc);
    void registerCommand(const char* cmd, const char* desc, CommandHandle handle);
    void registerCommandInterpreter(CommandInterpreter* ip);

    CommandItem* getCommand(char* cmd);
    CommandInterpreter* getCommandInterpreter(char* cmd);

    void searchCommand(bool fullmatch, char* basecmd, CommandItem** result, uint8_t& result_len);
    void searchCommandInterpreter(bool fullmatch, char* basecmd, CommandInterpreter** result, uint8_t& result_len);

    int8_t autocompleteHandle(char* cmd, bool displayOnly, char* parentcmd);
    int8_t executeCommand(char* cmd);
};

class Shell
{
    private:
    const char* prompt = SHELL_PROMPT;

    char input_buffer[SHELL_MAX_COMMAND_LENGTH];
    uint8_t input_count=0;
    bool prompt_lock=false;
    CommandInterpreter interpreter;

    HardwareSerial& serial;

    void _printprompt();
    void _backspace();
    void _execute();
    void _autocomplete();
    void _autocomplete_help();
    void _prompt_lock();

    public:
    Shell(HardwareSerial &serial=Serial);// must init serial first
    void begin();
    
    void registerCommand(const char* cmd, const char* desc, CommandInterpreter::CommandHandle handle);
    void registerCommandInterpreter(CommandInterpreter* handle);
    void inputChar(char c);
    void prompt_flush();
    
    void print(char buf);
    void print(int32_t buf);
    void print(uint32_t buf);
    void print(char* buf);
    void print(const char* buf);
    void println();
    void println(char* buf);
    void println(const char* buf);
};