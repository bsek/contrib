/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    A crash report for the browser: the task's trap handler writes the
    registers and the frame chain to RAM:owb-crash.txt and parks the task,
    so a fault inside generated (JIT) code can be read later without the
    system requester walking an untrusted frame chain. Addresses are also
    given relative to this program's code, so a symbol table resolves them.
*/

#include <exec/tasks.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>
#include <stdio.h>
#include <string.h>

extern int main(void);

static void owb_crash_handler(ULONG trapNum, struct ExceptionContext *ctx)
{
    struct Task *task = FindTask(NULL);
    BPTR file = Open("RAM:owb-crash.txt", MODE_NEWFILE);
    IPTR lower = (IPTR)task->tc_SPLower, upper = (IPTR)task->tc_SPUpper;
    IPTR base = (IPTR)&main;
    char line[160];
    int n;

    if (file)
    {
        snprintf(line, sizeof(line), "[OWB CRASH] trap=%lu task=%s main=%p\n",
                 (unsigned long)trapNum, task->tc_Node.ln_Name, (void *)base);
        Write(file, line, strlen(line));
        snprintf(line, sizeof(line), "pc=%p lr=%p sp=%p fp=%p cpsr=%08lx\n",
                 (void *)ctx->pc, (void *)ctx->lr, (void *)ctx->sp, (void *)ctx->fp,
                 (unsigned long)ctx->cpsr);
        Write(file, line, strlen(line));
        snprintf(line, sizeof(line), "pc-main=%lx lr-main=%lx\n",
                 (unsigned long)(ctx->pc - base), (unsigned long)(ctx->lr - base));
        Write(file, line, strlen(line));
        for (n = 0; n < 29; n++)
        {
            snprintf(line, sizeof(line), "x%d=%p\n", n, (void *)ctx->x[n]);
            Write(file, line, strlen(line));
        }
        /* The frame chain: fp -> {fp', lr}. Only frames inside the task's own stack. */
        {
            IPTR fp = ctx->fp;
            for (n = 0; n < 64 && fp >= lower && fp + 16 <= upper && (fp & 15) == 0; n++)
            {
                IPTR next = ((IPTR *)fp)[0], ret = ((IPTR *)fp)[1];
                snprintf(line, sizeof(line), "frame %d fp=%p ret=%p ret-main=%lx\n",
                         n, (void *)fp, (void *)ret, (unsigned long)(ret - base));
                Write(file, line, strlen(line));
                if (next <= fp) break;
                fp = next;
            }
        }
        Close(file);
    }
    /* Keep the task so the report can be read; the system stays usable. */
    for (;;)
        Wait(0);
}

void owb_crash_install(void)
{
    struct Task *task = FindTask(NULL);
    task->tc_TrapCode = owb_crash_handler;
    /* The load address, for symbolising a kernel [TRAP] line. */
    bug("[OWB] main=%p\n", (APTR)&main);
}
