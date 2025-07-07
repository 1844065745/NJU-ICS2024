/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"
#include <memory/paddr.h>

 #define CLOSE "\001\033[0m\002" // 关闭所有属性
 #define BLOD "\001\033[1m\002" // 强调、加粗、高亮
 #define BEGIN(x,y) "\001\033["#x";"#y"m\002" // x: 背景，y: 前景

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline(BEGIN(49, 32)"(nemu) "CLOSE);

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_help(char *args);

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  if (nemu_state.halt_ret != 0) {
    printf("NEMU has exited with code %d.\n", nemu_state.halt_ret);
  } else {
    printf("NEMU has exited successfully.\n");
  }
  printf("Bye!\n");
  return -1;
}

static int cmd_si(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int n = 1; // default step count is 1

  if (arg != NULL) {
    n = atoi(arg);
    if (n <= 0) {
      printf("Invalid step count: %s\n", arg);
    } else {
      cpu_exec(n);
    }
  } else {
    cpu_exec(n);
  }
  return 0;
}

static int cmd_info(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");

  if (arg != NULL) {
    if (strcmp(arg, "r") == 0) {
      isa_reg_display();
    } else if (strcmp(arg, "w") == 0) {
      // extern void print_wp();
      // print_wp();
      printf("(Watchpoints are not implemented yet.)\n");
    } else if (strcmp(arg, "c") == 0) {
      printf("NEMU state: %s\n", nemu_state.state == NEMU_RUNNING ? "RUNNING" : "HALTED");
      printf("Halt PC: 0x%08x\n", nemu_state.halt_pc);
      printf("cpu PC: 0x%08x\n", cpu.pc);
      printf("Halt return value: %d\n", nemu_state.halt_ret);
    } else {
      printf("Unknown argument '%s' for 'info' command.\n", arg);
    }
  }
  else {
    printf("Usage: info <r|w|c>\n");
    printf("  r - print registers\n");
    printf("  w - print watchpoints (not implemented yet)\n");
    printf("  c - print NEMU state and halt information\n");
  }
  return 0;
}

static int cmd_x(char *args) {
  /* extract the first argument */
  char *arg_N = strtok(NULL, " ");
  char *arg_EXPR = strtok(NULL, " ");
  
  if (arg_N == NULL || arg_EXPR == NULL) {
    printf("Usage: x <N> <EXPR>\n");
  } else {
    printf("N = %s, EXPR = %s\n", arg_N, arg_EXPR);
    uint8_t *addr = guest_to_host(0x80000000);           // 你想读取的地址
    uint8_t value = *(volatile uint8_t *)addr;  // 强制类型转换 + volatile
    printf("Byte at address %p: 0x%02x\n", addr, value);
  }

  return 0;
}

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Execute N step(s)", cmd_si },
  { "info", "Print program state", cmd_info },
  { "x", "Scan memory", cmd_x },

  /* TODO: Add more commands */

};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
