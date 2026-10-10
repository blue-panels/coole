/*
   The debugger plugin: what its backends have in common.

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "debug-backend.h"

void
debug_frame_free (gpointer data)
{
    debug_frame_t *frame = (debug_frame_t *) data;

    if (frame == NULL)
        return;
    g_free (frame->func);
    g_free (frame->file);
    g_free (frame->from);
    g_free (frame->address);
    g_free (frame->label);
    g_free (frame);
}

void
debug_variable_free (gpointer data)
{
    debug_variable_t *variable = (debug_variable_t *) data;

    if (variable == NULL)
        return;
    g_free (variable->name);
    g_free (variable->value);
    g_free (variable->ref);
    g_free (variable->expression);
    g_free (variable->type);
    g_free (variable);
}

void
debug_instruction_free (gpointer data)
{
    debug_instruction_t *instruction = (debug_instruction_t *) data;

    if (instruction == NULL)
        return;
    g_free (instruction->address);
    g_free (instruction->func);
    g_free (instruction->text);
    g_free (instruction->file);
    g_free (instruction);
}
