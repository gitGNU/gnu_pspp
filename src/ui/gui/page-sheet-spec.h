/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef PAGE_SHEET_SPEC
#define PAGE_SHEET_SPEC 1


struct sheet_spec_page ;
struct import_assistant;

/* Initializes IA's sheet_spec substructure. */
struct sheet_spec_page *sheet_spec_page_create (struct import_assistant *ia);

char *sheet_spec_gen_syntax (const struct import_assistant *ia);


#endif
