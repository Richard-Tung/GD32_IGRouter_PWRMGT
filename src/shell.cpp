/*
    Copyright 2026 Richard Tung <dyq@dyq.me>
*/

#include "shell.h"

void trimstr(char* str)
{
    if (!str) return;

    char* src = str;
    while (*src == ' ') src++;

    char* dst = str;
    bool prev_space = false;

    while (*src)
    {
        if (*src == ' ')
        {
            prev_space = true;
        }
        else
        {
            if(prev_space)
                *dst++ = ' ';
            *dst++ = *src;
            prev_space = false;
        }
        src++;
    }

    *dst = '\0';
}

void splitArgs(char* str, char* cmd, char* args, bool* cmd_has_tail_space)
{
    /*
        cmd, args must at least same size of str
    */

    if (!str || !cmd || !args) return;
    if(cmd_has_tail_space!=nullptr) *cmd_has_tail_space=false;

    char* idx=str;
    while (*idx == ' ') idx++;
    char* cmd_start=idx;
    char* search = strchr(idx, ' ');
    if (!search)
    {
        strcpy(cmd, idx);
        *args = '\0';
        return;
    }
    else 
    {
        idx=search;
        if(cmd_has_tail_space!=nullptr) *cmd_has_tail_space=true;
    }

    size_t cmd_len = idx - cmd_start;
    strncpy(cmd, cmd_start, cmd_len);
    cmd[cmd_len] = '\0';

    idx++;
    while (*idx == ' ') idx++;
    strcpy(args, idx);
}

//CommandInterpreter

CommandInterpreter::CommandInterpreter(Shell& s, const char* cmd, const char* desc): shell(s), cmd(cmd), description(desc)
{
    int i;
    for(i=0;i<SHELL_MAX_CMD_PER_INTP;i++)
    {
        cmdlist[i].cmd=nullptr;
        cmdlist[i].description=nullptr;
        cmdlist[i].handle=nullptr;
    }
    for(i=0;i<SHELL_MAX_SUB_INTP_PER_INTP;i++)
    {
        iplist[i]=nullptr;
    }
}

void CommandInterpreter::registerCommand(const char* cmd, const char* desc, CommandHandle handle)
{
    uint8_t i=cmd_count++;
    cmdlist[i].cmd=cmd;
    cmdlist[i].description=desc;
    cmdlist[i].handle=handle;
}

void CommandInterpreter::registerCommandInterpreter(CommandInterpreter* ip)
{
    iplist[ip_count++]=ip;
}

CommandInterpreter::CommandItem* CommandInterpreter::getCommand(char* cmd)
{
    for(int i=0;i<cmd_count;i++)
    {
        if(strcmp(cmdlist[i].cmd,cmd)==0)
            return &cmdlist[i];
    }
    return nullptr;
}

CommandInterpreter* CommandInterpreter::getCommandInterpreter(char* cmd)
{
    for(int i=0;i<ip_count;i++)
    {
        if(strcmp(iplist[i]->cmd,cmd)==0)
            return iplist[i];
    }
    return nullptr;
}

void CommandInterpreter::searchCommand(bool fullmatch, char* cmd, CommandItem** result, uint8_t& result_len)
{
    uint8_t count=0,i;
    for(i=0;i<SHELL_MAX_CMD_PER_INTP;i++)
    {
        if(cmdlist[i].handle==nullptr) continue;
        if(strlen(cmd)>strlen(cmdlist[i].cmd)) continue;
        if(fullmatch)
        {
            if(strcasecmp(cmd,cmdlist[i].cmd)==0)
                result[count++]=&cmdlist[i];
        }
        else
        {
            if(strncasecmp(cmd,cmdlist[i].cmd,strlen(cmd))==0)
                result[count++]=&cmdlist[i];
        }
        if(count>=result_len) break;
    }
    for(i=count;i<result_len;i++) result[i]=nullptr;
    result_len=count;
}

void CommandInterpreter::searchCommandInterpreter(bool fullmatch, char* cmd, CommandInterpreter** result, uint8_t& result_len)
{
    uint8_t count=0,i;
    for(i=0;i<SHELL_MAX_SUB_INTP_PER_INTP;i++)
    {
        if(iplist[i]==nullptr) continue;
        if(strlen(cmd)>strlen(iplist[i]->cmd)) continue;
        if(fullmatch)
        {
            if(strcasecmp(cmd,iplist[i]->cmd)==0)
                result[count++]=iplist[i];
        }
        else
        {
            if(strncasecmp(cmd,iplist[i]->cmd,strlen(cmd))==0)
                result[count++]=iplist[i];
        }
        if(count>=result_len) break;
    }
    for(i=count;i<result_len;i++) result[i]=nullptr;
    result_len=count;
}

int8_t CommandInterpreter::autocompleteHandle(char* basecmd, bool displayOnly, char* parentcmd)
{
    /*
        returns:
        0: complete
        -1: not found
        -2: ambiguous
    */
    // if(strlen(basecmd)<=0) return -1;

    char subcmd[SHELL_MAX_COMMAND_LENGTH],args[SHELL_MAX_COMMAND_LENGTH],fullcmd[SHELL_MAX_COMMAND_LENGTH];
    bool cmd_has_tail_space;
    splitArgs(basecmd,subcmd,args,&cmd_has_tail_space);
    if(parentcmd!=nullptr)
        snprintf(fullcmd,SHELL_MAX_COMMAND_LENGTH,"%s %s",parentcmd,subcmd);
    else
        snprintf(fullcmd,SHELL_MAX_COMMAND_LENGTH,"%s",subcmd);

    CommandItem* cmd_matches[SHELL_MAX_CMD_PER_INTP];
    CommandInterpreter* ip_matches[SHELL_MAX_SUB_INTP_PER_INTP];
    uint8_t cmd_count=SHELL_MAX_CMD_PER_INTP;
    uint8_t ip_count=SHELL_MAX_SUB_INTP_PER_INTP;

    this->searchCommand(false,subcmd,cmd_matches,cmd_count);
    this->searchCommandInterpreter(false,subcmd,ip_matches,ip_count);
    
    if(cmd_count==0 && ip_count==0) return -1;
    else if ((cmd_count+ip_count)==1)//one match, cmd or ip
    {
        if(displayOnly)
        {
            if(cmd_count==1)
            {
                this->shell.print(cmd_matches[0]->cmd);this->shell.print(": ");this->shell.print(cmd_matches[0]->description);this->shell.print(SHELL_NEW_LINE);
            }
            else //ip_count==1
            {
                ip_matches[0]->autocompleteHandle(args,displayOnly,fullcmd);
            }
            return 0;
        }
        else
        {
            if(cmd_count==1)
            {
                if(!cmd_has_tail_space) snprintf(basecmd,SHELL_MAX_COMMAND_LENGTH,"%s ",cmd_matches[0]->cmd);
                return 0;
            }
            else //ip_count==1
            {
                if(cmd_has_tail_space)
                {
                    int8_t result=ip_matches[0]->autocompleteHandle(args,displayOnly,fullcmd);
                    if(result==0)//only one, success
                        snprintf(basecmd,SHELL_MAX_COMMAND_LENGTH,"%s %s",subcmd,args);// basecmd=subcmd+" "+args;
                    return result;
                }
                else
                {
                    snprintf(basecmd,SHELL_MAX_COMMAND_LENGTH,"%s ",ip_matches[0]->cmd);//complete current
                    return 0;
                }
            }
        }
    }
    else//multiple match
    {
        //check lcp can match
        const char* match_names[SHELL_MAX_CMD_PER_INTP+SHELL_MAX_SUB_INTP_PER_INTP];
        uint32_t name_count=0;
        uint32_t name_min_len=UINT32_MAX;
        for(uint8_t i = 0; i < cmd_count; i++)
        {
            uint32_t len=strlen(cmd_matches[i]->cmd);
            match_names[name_count++] = cmd_matches[i]->cmd;
            if(len<name_min_len) name_min_len=len;
        }
        for(uint8_t i = 0; i < ip_count; i++)
        {
            uint32_t len=strlen(ip_matches[i]->cmd);
            match_names[name_count++] = ip_matches[i]->cmd;
            if(len<name_min_len) name_min_len=len;
        }
        uint32_t lcp_len=0;
        bool lcp_finish=false;
        for(uint32_t i=0;i<name_min_len;i++)//char index
        {
            char c=match_names[0][i];//get first match
            if(c=='\0') {lcp_finish=true;break;}
            for(uint8_t idx=0;idx<name_count;idx++)//strings index
            {
                char cp=match_names[idx][i];
                if(cp=='\0' || cp != c) {lcp_finish=true;break;}
            }
            if(lcp_finish) break;
            lcp_len++;
        }

        if(lcp_len>strlen(subcmd))
        {
            strncpy(basecmd,match_names[0],lcp_len);
            basecmd[lcp_len]='\0';
            return 0;
        }
        else
        {
            char buffer[SHELL_MAX_HELP_LENGTH];
            if(cmd_count>=1) for(int i=0;i<cmd_count;i++)
            {
                snprintf(buffer,SHELL_MAX_HELP_LENGTH,"%s: %s%s",cmd_matches[i]->cmd,cmd_matches[i]->description,SHELL_NEW_LINE);
                this->shell.print(buffer);
            }
            if(ip_count>=1) for(int i=0;i<ip_count;i++)
            {
                // ip_matches[i]->autocompleteHandle(args,true,fullcmd);
                snprintf(buffer,SHELL_MAX_HELP_LENGTH,"%s: %s%s",ip_matches[i]->cmd,ip_matches[i]->description,SHELL_NEW_LINE);
                this->shell.print(buffer);
            }
            return -2;
        }
    }
}

int8_t CommandInterpreter::executeCommand(char* basecmd)
{
    /*
    0: success, include no command
    -1: command not found
    -2: ambiguous command
    -3: command incomplete
    */

    trimstr(basecmd);
    if(strlen(basecmd)<=0) return 0;

    char subcmd[SHELL_MAX_COMMAND_LENGTH],args[SHELL_MAX_COMMAND_LENGTH];
    splitArgs(basecmd,subcmd,args);

    CommandItem* cmd_matches[SHELL_MAX_CMD_PER_INTP];
    CommandInterpreter* ip_matches[SHELL_MAX_SUB_INTP_PER_INTP];
    uint8_t cmd_count=SHELL_MAX_CMD_PER_INTP;
    uint8_t ip_count=SHELL_MAX_SUB_INTP_PER_INTP;

    this->searchCommand(true,subcmd,cmd_matches,cmd_count);
    this->searchCommandInterpreter(true,subcmd,ip_matches,ip_count);
    if(cmd_count==0 && ip_count==0)
    {
        return -1;
    }
    else if ((cmd_count+ip_count)==1)
    {
        if(cmd_count==1)
        {
            cmd_matches[0]->handle(this->shell, args);
            return 0;
        }
        else
        {
            if(strlen(args)<=0) return -3;
            return ip_matches[0]->executeCommand(args);
        }
    }
    else
    {
        return -2;
    }
}


//Shell

Shell::Shell(HardwareSerial& s): serial(s), interpreter(*this, "root", "Available Commands")
{

}

void Shell::begin()
{
    _printprompt();
}

void Shell::_printprompt()
{
    prompt_lock=false;
    serial.print('\r');
    serial.print(this->prompt);
    serial.print(input_buffer);
}

void Shell::_backspace()
{
    prompt_flush();
    if(input_count>0)
    {
        input_buffer[--input_count]='\0';
        serial.print('\b');
        serial.print(' ');
        serial.print('\b');
    }
}

void Shell::_execute()
{
    serial.print(SHELL_NEW_LINE);
    int8_t result=this->interpreter.executeCommand(input_buffer);
    char buffer[SHELL_MAX_HELP_LENGTH];
    switch(result)
    {
        case -1:
            snprintf(buffer,SHELL_MAX_HELP_LENGTH,"Command not found: %s%s",input_buffer,SHELL_NEW_LINE);
            this->print(buffer);
            break;
        case -2:
            snprintf(buffer,SHELL_MAX_HELP_LENGTH,"Ambiguous command: %s%s",input_buffer,SHELL_NEW_LINE);
            this->print(buffer);
            break;
        case -3:
            snprintf(buffer,SHELL_MAX_HELP_LENGTH,"Command incomplete: %s%s",input_buffer,SHELL_NEW_LINE);
            this->print(buffer);
            break;
        case 0:
        default:
            break;
    }
    input_buffer[0]='\0';
    input_count=0;
    _printprompt();
}

void Shell::_autocomplete()
{
    prompt_flush();
    this->interpreter.autocompleteHandle(input_buffer,false,nullptr);
    input_count=strlen(input_buffer);
    _printprompt();
}

void Shell::_autocomplete_help()
{
    prompt_flush();
    serial.print('?');
    serial.print(SHELL_NEW_LINE);
    this->interpreter.autocompleteHandle(input_buffer,true,nullptr);
    input_count=strlen(input_buffer);
    _printprompt();
}

void Shell::registerCommand(const char* cmd, const char* desc, CommandInterpreter::CommandHandle handle)
{
    this->interpreter.registerCommand(cmd,desc,handle);
}

void Shell::registerCommandInterpreter(CommandInterpreter* handle)
{
    this->interpreter.registerCommandInterpreter(handle);
}

void Shell::inputChar(char c)
{
    prompt_flush();
    switch(c)
    {
        case '?':
            _autocomplete_help();
            break;
        case '\t':
            _autocomplete();
            break;
        case '\b':
        case 0x7f:
            _backspace();
            break;
        case '\r':
        case '\n':
            _execute();
            break;
        default:
            if(isPrintable(c))
            {
                if(this->input_count+1>=SHELL_MAX_COMMAND_LENGTH) break;
                this->input_buffer[this->input_count++]=c;
                this->input_buffer[this->input_count]='\0';
                serial.print(c);
            }
            break;
    }
}

void Shell::_prompt_lock()
{
    if(prompt_lock) return;//already locked
    //clear prompt line
    // this->serial.print('\r');
    // for(int i=0;i<SHELL_SCREEN_WIDTH;i++) this->serial.print(' ');
    // this->serial.print('\r');
    serial.print("\033[2K\r");
    prompt_lock=true;
}

void Shell::prompt_flush()
{
    if(prompt_lock) _printprompt();
}

void Shell::print(char buf)
{
    _prompt_lock();
    this->serial.print(buf);
}

void Shell::print(int32_t buf)
{
    _prompt_lock();
    this->serial.print(buf);
}

void Shell::print(uint32_t buf)
{
    _prompt_lock();
    this->serial.print(buf);
}

void Shell::print(char* buf)
{
    _prompt_lock();
    this->serial.print(buf);
}

void Shell::print(const char* buf)
{
    _prompt_lock();
    this->serial.print(buf);
}

void Shell::println()
{
    _prompt_lock();
    this->serial.print(SHELL_NEW_LINE);
}

void Shell::println(char* buf)
{
    this->print(buf);
    this->serial.print(SHELL_NEW_LINE);
}

void Shell::println(const char* buf)
{
    this->print(buf);
    this->serial.print(SHELL_NEW_LINE);
}