/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "cli/cli_api.h"

#include "cli_conf.h"
#include "cli_adapt.h"
#include "k_api.h"
#include "debug_api.h"

#define RET_CHAR '\n'
#define END_CHAR '\r'
#define PROMPT   "# "
#define EXIT_MSG "exit"

#if (CLI_MINIMUM_MODE > 0)
#undef CLI_INBUF_SIZE
#define CLI_INBUF_SIZE 128

#undef CLI_OUTBUF_SIZE
#define CLI_OUTBUF_SIZE 32

#undef CLI_MAX_COMMANDS
#define CLI_MAX_COMMANDS 64

#undef CLI_MAX_ARG_NUM
#define CLI_MAX_ARG_NUM 8

#undef CLI_MAX_ONCECMD_NUM
#define CLI_MAX_ONCECMD_NUM 1
#endif

#if (RHINO_CONFIG_UCLI > 0)
#include "task_group.h"
#include "res.h"
#endif

struct cli_status {
    int32_t  inited;
    uint32_t num;
    int32_t  echo_disabled;
    uint32_t bp; /* Buffer pointer */

    char  inbuf[CLI_INBUF_SIZE];
    char *outbuf;

    const struct cli_command_st *cmds[CLI_MAX_COMMANDS];

#if (RHINO_CONFIG_UCLI > 0)
    klist_t ucmd_list_head;
#endif

#if (CLI_MINIMUM_MODE <= 0)
    int32_t his_idx;
    int32_t his_cur;
    char    history[CLI_INBUF_SIZE];
#endif
};

extern int32_t cli_register_default_commands(void);

static struct cli_status *g_cli = NULL;

static char    g_cli_tag[64] = {0};
static uint8_t g_cli_tag_len =  0;

static const struct cli_command_st *lookup_command(char *name, int len)
{
    int i = 0;
    int n = 0;

    while (i < CLI_MAX_COMMANDS && n < g_cli->num) {
        if (g_cli->cmds[i]->name == NULL) {
            i++;
            continue;
        }
        /* See if partial or full match is expected */
        if (len != 0) {
            if (!strncmp(g_cli->cmds[i]->name, name, len)) {
                return g_cli->cmds[i];
            }
        } else {
            if (!strcmp(g_cli->cmds[i]->name, name)) {
                return g_cli->cmds[i];
            }
        }

        i++;
        n++;
    }

    return NULL;
}

#if (RHINO_CONFIG_UCLI > 0)
static const struct ucli_command* lookup_user_command(char *name, int len)
{
    struct ucli_command *ucmd;
    klist_t             *head;
    klist_t             *iter;
    klist_t             *next;

    head = &g_cli->ucmd_list_head;
    iter = head->next;

    /* check whether the cmd has been registered */
    while (iter != head) {
        next = iter->next;
        ucmd = krhino_list_entry(iter, struct ucli_command, node);
        if (len) {
            if (!strncmp(ucmd->cmd->name, name, len)) {
                return ucmd;
            }
        } else {
            if (!strcmp(ucmd->cmd->name, name)) {
                return ucmd;
            }
        }
        iter = next;
    }

    return NULL;
}
#endif

static int32_t proc_onecmd(int argc, char *argv[])
{
    int32_t i = 0;
    uint8_t tmp = 0;

    const char *p = NULL;

    const struct cli_command_st *command = NULL;

#if (RHINO_CONFIG_UCLI > 0)
    const struct ucli_command *ucmd;

    task_group_t  *group;
    void          *user_ptr;
    char         **argv_ptr;
    char          *ptr;
    size_t         size;
    size_t         str_len;
    int            arg_cnt;
    ucli_msg_t     ucli_msg;
#endif

    if (argc < 1) {
        return 0;
    }

    if (!g_cli->echo_disabled) {
        tmp = g_cli_tag_len;
        g_cli_tag_len = 0;
        cli_printf("\r\n");

        g_cli_tag_len = tmp;
    }

    /*
     * Some comamands can allow extensions like foo.a, foo.b and hence
     * compare commands before first dot.
     */
    i = ((p = strchr(argv[0], '.')) == NULL) ? 0 : (p - argv[0]);

    command = lookup_command(argv[0], i);
    if (command == NULL) {
#if (RHINO_CONFIG_UCLI > 0)
        ucmd = lookup_user_command(argv[0], i);
        if (ucmd != NULL) {
            if (argc > 0) {
                size = 0;
                for (arg_cnt = 0; arg_cnt < argc; arg_cnt++) {
                     size += strlen(argv[arg_cnt]) + 1;
                }
                size += arg_cnt * sizeof(void*);
                group = task_group_get_by_pid(ucmd->owner_pid);
                user_ptr = res_malloc(group->pid, size);
                if (user_ptr) {
                    memset(user_ptr, 0, size);
                    argv_ptr = (char**)user_ptr;
                    ptr = (char*)user_ptr + argc * sizeof(void*);
                    for (arg_cnt = 0; arg_cnt < argc; arg_cnt++) {
                        str_len = strlen(argv[arg_cnt]);
                        memcpy(ptr, argv[arg_cnt], str_len);
                        argv_ptr[arg_cnt] = ptr;
                        ptr += str_len + 1;
                    }
                    ucli_msg.argc = argc;
                    ucli_msg.argv = argv_ptr;
                }
            } else {
                ucli_msg.argc = 0;
                ucli_msg.argv = NULL;
            }
            ucli_msg.func = (void*)ucmd->cmd->function;
            krhino_buf_queue_send(ucmd->push_queue, (void*)&ucli_msg, sizeof(ucli_msg_t));
            return 0;
        }

        return 1;
#else
    return 1;
#endif
    }

    g_cli->outbuf = cli_malloc(CLI_OUTBUF_SIZE);
    if (NULL == g_cli->outbuf) {
        cli_printf("Error! cli alloc mem fail!\r\n");
        return 1;
    }
    memset(g_cli->outbuf, 0, CLI_OUTBUF_SIZE);

    command->function(g_cli->outbuf, CLI_OUTBUF_SIZE, argc, argv);
    cli_printf("%s", g_cli->outbuf);

    cli_free(g_cli->outbuf);
    g_cli->outbuf = NULL;
    return 0;
}

static int32_t cli_handle_input(char *inbuf)
{
    struct
    {
        unsigned inArg : 1;
        unsigned inQuote : 1;
        unsigned done : 1;
    } stat;
    static char *argvall[CLI_MAX_ONCECMD_NUM][CLI_MAX_ARG_NUM];
    int32_t      argcall[CLI_MAX_ONCECMD_NUM] = { 0 };

    int32_t  cmdnum = 0;
    int32_t *pargc  = &argcall[0];
    int32_t  i      = 0;
    int32_t  ret    = 0;

    memset((void *)&argvall, 0, sizeof(argvall));
    memset((void *)&argcall, 0, sizeof(argcall));
    memset(&stat, 0, sizeof(stat));

    do {
        switch (inbuf[i]) {
            case '\0':
                if (stat.inQuote) {
                    return 2;
                }
                stat.done = 1;
                break;

            case '"':
                if (i > 0 && inbuf[i - 1] == '\\' && stat.inArg) {
                    memcpy(&inbuf[i - 1], &inbuf[i], strlen(&inbuf[i]) + 1);
                    --i;
                    break;
                }
                if (!stat.inQuote && stat.inArg) {
                    break;
                }
                if (stat.inQuote && !stat.inArg) {
                    return 2;
                }

                if (!stat.inQuote && !stat.inArg) {
                    stat.inArg   = 1;
                    stat.inQuote = 1;
                    (*pargc)++;
                    argvall[cmdnum][(*pargc) - 1] = &inbuf[i + 1];
                } else if (stat.inQuote && stat.inArg) {
                    stat.inArg   = 0;
                    stat.inQuote = 0;
                    inbuf[i]     = '\0';
                }
                break;

            case ' ':
                if (i > 0 && inbuf[i - 1] == '\\' && stat.inArg) {
                    memcpy(&inbuf[i - 1], &inbuf[i], strlen(&inbuf[i]) + 1);
                    --i;
                    break;
                }
                if (!stat.inQuote && stat.inArg) {
                    stat.inArg = 0;
                    inbuf[i]   = '\0';
                }
                break;

            case ';':
                if (i > 0 && inbuf[i - 1] == '\\' && stat.inArg) {
                    memcpy(&inbuf[i - 1], &inbuf[i], strlen(&inbuf[i]) + 1);
                    --i;
                    break;
                }
                if (stat.inQuote) {
                    return 2;
                }
                if (!stat.inQuote && stat.inArg) {
                    stat.inArg = 0;
                    inbuf[i]   = '\0';

                    if (*pargc) {
                        if (++cmdnum < CLI_MAX_ONCECMD_NUM) {
                            pargc = &argcall[cmdnum];
                        }
                    }
                }

                break;

            default:
                if (!stat.inArg) {
                    stat.inArg = 1;
                    (*pargc)++;
                    argvall[cmdnum][(*pargc) - 1] = &inbuf[i];
                }
                break;
        }
    } while (!stat.done && ++i < CLI_INBUF_SIZE && cmdnum < CLI_MAX_ONCECMD_NUM &&
             (*pargc) < CLI_MAX_ARG_NUM);

    if (stat.inQuote) {
        return 2;
    }

    for (i = 0; i <= cmdnum && i < CLI_MAX_ONCECMD_NUM; i++) {
        ret |= proc_onecmd(argcall[i], argvall[i]);
    }

    return ret;
}

/**
 * @brief Perform basic tab-completion on the input buffer
 *
 * @param[in] inbuf the input buffer
 * @param[in] bp    the current buffer pointer
 *
 * @return none
 *
 */
static void cli_tab_complete(char *inbuf, unsigned int *bp)
{
    int32_t i, n, m;

    const char *fm = NULL;

    i = n = m = 0;

    cli_printf("\r\n");

    /* show matching commands */
    for (i = 0; i < CLI_MAX_COMMANDS && n < g_cli->num; i++) {
        if (g_cli->cmds[i]->name != NULL) {
            if (!strncmp(inbuf, g_cli->cmds[i]->name, *bp)) {
                m++;
                if (m == 1) {
                    fm = g_cli->cmds[i]->name;
                } else if (m == 2)
                    cli_printf("%s %s ", fm, g_cli->cmds[i]->name);
                else
                    cli_printf("%s ", g_cli->cmds[i]->name);
            }
            n++;
        }
    }

    /* there's only one match, so complete the line */
    if (m == 1 && fm) {
        n = strlen(fm) - *bp;
        if (*bp + n < CLI_INBUF_SIZE) {
            memcpy(inbuf + *bp, fm + *bp, n);
            *bp += n;
            inbuf[(*bp)++] = ' ';
            inbuf[*bp]     = '\0';
        }
    }
    if (m >= 2) {
        cli_printf("\r\n");
    }

    /* just redraw input line */
    cli_printf("%s%s", PROMPT, inbuf);
}

#if (CLI_MINIMUM_MODE <= 0)

static void cli_history_input(void)
{
    char    *inbuf    = g_cli->inbuf;
    int32_t  charnum  = strlen(g_cli->inbuf) + 1;
    int32_t  his_cur  = g_cli->his_cur;
    int32_t  left_num = CLI_INBUF_SIZE - his_cur;

    char    lastchar;
    int32_t tmp_idx;

    g_cli->his_idx = his_cur;

    if (left_num >= charnum) {
        tmp_idx  = his_cur + charnum - 1;
        lastchar = g_cli->history[tmp_idx];
        strncpy(&(g_cli->history[his_cur]), inbuf, charnum);

    } else {
        tmp_idx  = (his_cur + charnum - 1) % CLI_INBUF_SIZE;
        lastchar = g_cli->history[tmp_idx];

        strncpy(&(g_cli->history[his_cur]), inbuf, left_num);
        strncpy(&(g_cli->history[0]), inbuf + left_num, charnum - left_num);
    }
    tmp_idx = (tmp_idx + 1) % CLI_INBUF_SIZE;

    g_cli->his_cur = tmp_idx;

    /*overwrite*/
    if ('\0' != lastchar) {

        while (g_cli->history[tmp_idx] != '\0') {
            g_cli->history[tmp_idx] = '\0';

            tmp_idx = (tmp_idx + 1) % CLI_INBUF_SIZE;
        }
    }
}

static void cli_up_history(char *inaddr)
{
    int index;
    int lastindex = 0;

    lastindex = g_cli->his_idx;
    index     = (g_cli->his_idx - 1 + CLI_INBUF_SIZE) % CLI_INBUF_SIZE;

    while ((g_cli->history[index] == '\0') && (index != g_cli->his_idx)) {
        index = (index - 1 + CLI_INBUF_SIZE) % CLI_INBUF_SIZE;
    }
    if (index != g_cli->his_idx) {
        while (g_cli->history[index] != '\0') {
            index = (index - 1 + CLI_INBUF_SIZE) % CLI_INBUF_SIZE;
        }
        index = (index + 1) % CLI_INBUF_SIZE;
    }
    g_cli->his_idx = index;

    while (g_cli->history[lastindex] != '\0') {

        *inaddr++ = g_cli->history[lastindex];
        lastindex = (lastindex + 1) % CLI_INBUF_SIZE;
    }
    *inaddr = '\0';

    return;
}

static void cli_down_history(char *inaddr)
{
    int index;
    int lastindex = 0;

    lastindex = g_cli->his_idx;
    index     = g_cli->his_idx;

    while ((g_cli->history[index] != '\0')) {
        index = (index + 1) % CLI_INBUF_SIZE;
    }
    if (index != g_cli->his_idx) {
        while (g_cli->history[index] == '\0') {
            index = (index + 1) % CLI_INBUF_SIZE;
        }
    }
    g_cli->his_idx = index;

    while (g_cli->history[lastindex] != '\0') {
        *inaddr++ = g_cli->history[lastindex];
        lastindex = (lastindex + 1) % CLI_INBUF_SIZE;
    }

    *inaddr = '\0';

    return;
}

#endif

/**
 * @brief Get an input line
 *
 * @param[in/out] inbuf poiner to the input buffer
 * @param[out]    bp    the current buffer pointer
 *
 * @return 1 if there is input, 0 if the line should be ignored
 *
 */
static int32_t cli_get_input(char *inbuf, uint32_t *bp)
{
    char c;
    int32_t esc  =  0;
    int32_t key1 = -1;
    int32_t key2 = -1;
    uint8_t cli_tag_len =  0;
    ktask_t *task_to_cancel;

    if (inbuf == NULL) {
        cli_printf("input null\r\n");
        return 0;
    }

    while (cli_getchar(&c) == 1) {
        if (c == RET_CHAR || c == END_CHAR) { /* end of input line */
            inbuf[*bp] = '\0';
            *bp = 0;
            if (cli_tag_len > 0) {
                g_cli_tag_len = cli_tag_len;
                cli_tag_len = 0;
            }
            return 1;
        }

        // if (c == 0x1b) { /* escape sequence */
        if (0) { /* CTRL+C *///-------modify by jintang for tub communication
            esc  = 1;
            key1 = -1;
            key2 = -1;
            continue;
        }

        // if (c == 0x3) { /* CTRL+C */ //-------modify by jintang for tub communication
        if (0) { /* CTRL+C *///-------modify by jintang for tub communication
            task_to_cancel = debug_task_find("cpuusage");
            if (task_to_cancel != NULL) {
                krhino_task_cancel(task_to_cancel);
            }
            continue;
        }

        if (esc) {
            if (key1 < 0) {
                key1 = c;
                if (key1 != 0x5b) {
                    /* not '[' */
                    inbuf[*bp] = 0x1b;
                    (*bp)++;

                    inbuf[*bp] = key1;
                    (*bp)++;

                    if (!g_cli->echo_disabled) {
                        cli_printf("\x1b%c", key1); /* Ignore the cli tag */
                    }
                    esc = 0;
                }
                continue;
            }

            if (key2 < 0) {
                key2 = c;
                if (key2 == 't') {
                    g_cli_tag[0]  = 0x1b;
                    g_cli_tag[1]  = key1;
                    cli_tag_len = 2;
                }
            }

            if (key2 != 0x41 && key2 != 0x42 && key2 != 't') {
                /* not UP key, not DOWN key, not ESC_TAG */
                inbuf[*bp] = 0x1b;
                (*bp)++;

                inbuf[*bp] = key1;
                (*bp)++;

                inbuf[*bp] = key2;
                (*bp)++;

                g_cli_tag[0]  = '\x0';
                cli_tag_len = 0;
                esc           = 0;

                if (!g_cli->echo_disabled) {
                    cli_printf("\x1b%c%c", key1, key2);
                }
                continue;
            }

#if CLI_MINIMUM_MODE > 0
            if (key2 == 0x41 || key2 == 0x42) {
                /* UP or DWOWN key */
                cli_printf("\r\n" PROMPT
                        "Warning! mini cli mode do not support history cmds!");
            }
#else
            if (key2 == 0x41 || key2 == 0x42) {
                /* UP or DWOWN key */
                if (key2 == 0x41) {
                    cli_up_history(inbuf);
                } else {
                    cli_down_history(inbuf);
                }


                *bp           = strlen(inbuf);
                g_cli_tag[0]  = '\x0';
                cli_tag_len = 0;
                esc           = 0;

                cli_printf("\r\n" PROMPT "%s", inbuf);
                continue;
            }
#endif
            if (key2 == 't') {
                /* ESC_TAG */
                if (cli_tag_len >= sizeof(g_cli_tag)) {
                    g_cli_tag[0]  = '\x0';
                    cli_tag_len = 0;
                    esc           = 0;

                    cli_printf("Error: cli tag buffer overflow\r\n");
                    continue;
                }

                g_cli_tag[cli_tag_len++] = c;
                if (c == 'm') {
                    g_cli_tag[cli_tag_len++] = '\x0';

                    if (!g_cli->echo_disabled) {
                        cli_printf("%s", g_cli_tag);
                    }
                    esc = 0;
                }
                continue;
            }
        }

        inbuf[*bp] = c;
        if ((c == 0x08) || (c == 0x7f)) {
            if (*bp > 0) {
                (*bp)--;

                if (!g_cli->echo_disabled) {
                    cli_printf("%c %c", 0x08, 0x08);
                }
            }
            continue;
        }

        if (c == '\t') {
            inbuf[*bp] = '\0';
            cli_tab_complete(inbuf, bp);
            continue;
        }

        if (!g_cli->echo_disabled) {
            cli_printf("%c", c);
        }

        (*bp)++;
        if (*bp >= CLI_INBUF_SIZE) {
            cli_printf("Error: input buffer overflow\r\n");
            cli_printf(PROMPT);
            *bp = 0;
            return 0;
        }
        if (*bp == 5) {//-------create by jintang for tub communication
            if((inbuf[0]==0x02)
                &&(inbuf[1]==0x3A)
                &&(inbuf[2]==0x03)
                &&(inbuf[3]==0xFF)
                &&(inbuf[4]==0x01)
            )
            {
                printf("do_awss_reset()");//---jintang modify
                extern  void do_awss_reset();
                do_awss_reset();
            }
        }
    }
    *bp = 0;//-------add by jintang for tub communication

    return 0;
}

/**
 * @brief Print out a bad command string
 *
 * @param[in] cmd_string the command string
 *
 * @return none
 *
 * @Note print including a hex representation of non-printable characters.
 * Non-printable characters show as "\0xXX".
 */
static void cli_print_bad_command(char *cmd_string)
{
    if (cmd_string != NULL) {
        cli_printf("command '%s' not found\r\n", cmd_string);
    }
}


/**
 * @brief Main CLI processing loop
 *
 * @param[in] data pointer to the process arguments
 *
 * @return none
 *
 * @Note Waits to receive a command buffer pointer from an input collector,
 * and then process. it must cleanup the buffer when done with it.
 * Input collectors handle their own lexical analysis and must pass complete
 * command lines to CLI.
 *
 */
void cli_main(void *data)
{
    int32_t ret;

    char *msg = NULL;

    while (!cli_task_cancel_check()) {
        if (cli_get_input(g_cli->inbuf, &g_cli->bp) != 0) {
            msg = g_cli->inbuf;

#if (CLI_MINIMUM_MODE <= 0)
            if (strlen(g_cli->inbuf) > 0) {
                cli_history_input();
            }
#endif

            ret = cli_handle_input(msg);
            if (ret == CLI_ERR_BADCMD) {
                cli_print_bad_command(msg);
            } else if (ret == CLI_ERR_SYNTAX) {
                cli_printf("syntax error\r\n");
            }

            // cli_printf("\r\n");//----- comment by jintang for tub communication
            g_cli_tag[0]  = '\x0';
            g_cli_tag_len = 0;
            // cli_printf(PROMPT);//----- comment by jintang for tub communication
        }
    }

    cli_printf("CLI exited\r\n");
    cli_free(g_cli);
    g_cli = NULL;

    cli_task_exit();
}

#if (RHINO_CONFIG_UCLI > 0)
klist_t* cli_get_ucmd_list(void)
{
    if (NULL != g_cli)
        return &g_cli->ucmd_list_head;
    else
        return NULL;
}

int cli_process_init(int pid)
{
    kbuf_queue_t *cli_buf_q;
    task_group_t *group;
    int ret;

    group = task_group_get_by_pid(pid);

    ret = krhino_fix_buf_queue_dyn_create(&cli_buf_q,
                                          "cli_buf_queue",
                                          sizeof(ucli_msg_t),
                                          2);
    if (ret != RHINO_SUCCESS) {
        return -1;
    }

    group->cli_q = cli_buf_q;

    return 0;
}

void cli_process_exit(int pid)
{
    struct ucli_command *ucmd;
    klist_t             *head;
    klist_t             *iter;

    head = &g_cli->ucmd_list_head;
    iter = head->next;
    while (iter != head) {
        ucmd = krhino_list_entry(iter, struct ucli_command, node);
        iter = iter->next;
        if (ucmd->owner_pid == pid) {
            klist_rm(&ucmd->node);
            cli_free(ucmd);
        }
    }
}

void cli_process_destory(int pid)
{
    task_group_t *group;

    group = task_group_get_by_pid(pid);
    if (group->cli_q) {
        krhino_buf_queue_dyn_del(group->cli_q);
        group->cli_q = NULL;
    }
}
#endif

int32_t cli_init(void)
{
    int32_t ret;

    g_cli = (struct cli_status *)cli_malloc(sizeof(struct cli_status));
    if (g_cli == NULL) {
        return CLI_ERR_NOMEM;
    }

    memset((void *)g_cli, 0, sizeof(struct cli_status));

    ret = cli_task_create("cli", cli_main, NULL, CLI_STACK_SIZE, CLI_TASK_PRIORITY);
    if (ret != CLI_OK) {
        cli_printf("Error: Failed to create cli thread: %d\r\n", ret);
        goto init_err;
    }

#if (RHINO_CONFIG_UCLI > 0)
    klist_init(&g_cli->ucmd_list_head);
#endif

    g_cli->inited        = 1;
    g_cli->echo_disabled = 1;//----jintang modify 0 to 1

    ret = cli_register_default_commands();
    if (ret != CLI_OK) {
        cli_printf("Error: register built-in commands failed");
        goto init_err;
    }

    return CLI_OK;

init_err:
    if (g_cli != NULL) {
        cli_free(g_cli);
        g_cli = NULL;
    }

    return ret;
}

int32_t cli_stop(void)
{
    cli_task_cancel();

    return CLI_OK;
}

char *cli_tag_get(void)
{
    return g_cli_tag;
}

int32_t cli_register_command(const struct cli_command_st *cmd)
{
    int32_t i = 0;

#if (RHINO_CONFIG_UCLI > 0)
    klist_t             *head;
    struct ucli_command *ucmd;
    klist_t             *iter, *next;
    ktask_t             *cur_task;
    task_group_t        *group;
#endif

    if (g_cli == NULL) {
        return CLI_ERR_DENIED;
    }

    if (!cmd->name || !cmd->function) {
        return CLI_ERR_INVALID;
    }

    if (g_cli->num >= CLI_MAX_COMMANDS) {
        return CLI_ERR_NOMEM;
    }

    /*
     * Check if the command has already been registered.
     * Return 0, if it has been registered.
     */
    for (i = 0; i < g_cli->num; i++) {
        if (g_cli->cmds[i] == cmd) {
            return CLI_OK;
        }
    }

#if (RHINO_CONFIG_UCLI > 0)
    cur_task = krhino_cur_task_get();
    group = cur_task->task_group;
    if (NULL == group) {
        goto register_kernel_cmd;
    }

    head = &g_cli->ucmd_list_head;
    iter = head->next;
    while (iter != head) {
        next = iter->next;
        ucmd = krhino_list_entry(iter, struct ucli_command, node);
        if (!strcmp(ucmd->cmd->name, cmd->name)) {
            cli_printf("Warning: user cmd %s is already registered\r\n",
                       cmd->name);
            return CLI_OK;
        }
        iter = next;
    }

    ucmd = (struct ucli_command*)cli_malloc(sizeof(struct ucli_command));
    if (NULL == ucmd) {
        return CLI_ERR_NOMEM;
    }

    ucmd->cmd = cmd;
    ucmd->push_queue = group->cli_q;
    ucmd->owner_pid = group->pid;
    klist_add(head, &ucmd->node);
    return CLI_OK;
register_kernel_cmd:
#endif

    g_cli->cmds[g_cli->num++] = cmd;

    return CLI_OK;
}

int32_t cli_unregister_command(const struct cli_command_st *cmd)
{
    int32_t remaining_cmds;
    int32_t i = 0;
#if (RHINO_CONFIG_UCLI > 0)
    struct ucli_command *ucmd;
    klist_t             *head;
    klist_t             *iter;
    klist_t             *next;
#endif

    if (g_cli == NULL) {
        return CLI_ERR_DENIED;
    }

    if (!cmd->name || !cmd->function) {
        return CLI_ERR_INVALID;
    }

#if (RHINO_CONFIG_UCLI > 0)
    head = &g_cli->ucmd_list_head;
    iter = head->next;
    while (iter != head) {
        next = iter->next;
        ucmd = krhino_list_entry(iter, struct ucli_command, node);
        if (!strcmp(ucmd->cmd->name, cmd->name)) {
            cli_printf("%s: unregister ucmd %s\r\n",
                       __func__, ucmd->cmd->name);
            klist_rm(&ucmd->node);
            cli_free(ucmd);
            return CLI_OK;
        }
        iter = next;
    }
#endif

    for (i = 0; i < g_cli->num; i++) {
        if (g_cli->cmds[i] == cmd) {
            g_cli->num--;

            remaining_cmds = g_cli->num - i;
            if (remaining_cmds > 0) {
                memmove(&g_cli->cmds[i], &g_cli->cmds[i + 1],
                        (remaining_cmds * sizeof(struct cli_command_st *)));
            }

            g_cli->cmds[g_cli->num] = NULL;

            return CLI_OK;
        }
    }

    return CLI_ERR_NOMEM;
}

int32_t cli_register_commands(const struct cli_command_st *cmds, int32_t num)
{
    int32_t i, err;

    for (i = 0; i < num; i++) {
        if ((err = cli_register_command(cmds++)) != 0) {
            return err;
        }
    }

    return CLI_OK;
}

int32_t cli_unregister_commands(const struct cli_command_st *cmds, int32_t num)
{
    int32_t i, err;

    for (i = 0; i < num; i++) {
        if ((err = cli_unregister_command(cmds++)) != 0) {
            return err;
        }
    }

    return CLI_OK;
}

int32_t cli_printf(const char *buffer, ...)
{
    va_list ap;

    int32_t sz, len;

    char *pos     = NULL;
    char *message = NULL;

    message = (char *)cli_malloc(CLI_OUTBUF_SIZE);
    if (message == NULL) {
        return CLI_ERR_NOMEM;
    }

    memset(message, 0, CLI_OUTBUF_SIZE);

    sz = 0;
    if (g_cli_tag_len > 0) {
        len = strlen(g_cli_tag);
        strncpy(message, g_cli_tag, len);
        sz = len;
    }

    pos = message + sz;

    va_start(ap, buffer);
    len = vsnprintf(pos, CLI_OUTBUF_SIZE - sz, buffer, ap);
    va_end(ap);

    if (len <= 0) {
        cli_free(message);

        return CLI_OK;
    }

    cli_putstr(message);
    cli_free(message);

    return CLI_OK;
}

int32_t cli_get_commands_num(void)
{
    return g_cli->num;
}

struct cli_command_st *cli_get_command(int32_t index)
{
    return (struct cli_command_st *)(g_cli->cmds[index]);
}

int32_t cli_get_echo_status(void)
{
    return g_cli->echo_disabled;
}

int32_t cli_set_echo_status(int32_t status)
{
    g_cli->echo_disabled = status;

    return CLI_OK;
}
