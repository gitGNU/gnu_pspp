/* PSPP - a program for statistical analysis.
   Copyright (C) 2017 Free Software Foundation, Inc.

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

#include <config.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/data-parser.h"
#include "language/data-io/data-reader.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/inpt-pgm.h"
#include "language/data-io/placement-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"

#include "gl/xsize.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* DATA LIST transformation data. */
struct data_list_trns
  {
    struct data_parser *parser; /* Parser. */
    struct dfm_reader *reader;  /* Data file reader. */
    struct variable *end;	/* Variable specified on END subcommand. */
  };

static trns_free_func data_list_trns_free;
static trns_proc_func data_list_trns_proc;

enum diagonal
  {
    DIAGONAL,
    NO_DIAGONAL
  };

enum triangle
  {
    LOWER,
    UPPER,
    FULL
  };

struct matrix_format
{
  enum triangle triangle;
  enum diagonal diagonal;
  const struct variable *rowtype;
  const struct variable *varname;
  int n_continuous_vars;
};

/*
valid rowtype_ values:
  CORR,
  COV,
  MAT,


  MSE,
  DFE,
  MEAN,
  STDDEV (or SD),
  N_VECTOR (or N),
  N_SCALAR,
  N_MATRIX,
  COUNT,
  PROX.
*/

/* Sets the value of OUTCASE which corresponds to MFORMAT's varname variable
   to the string STR. VAR must be of type string.
 */
static void
set_varname_column (struct ccase *outcase, const struct matrix_format *mformat,
     const char *str, int len)
{
  const struct variable *var = mformat->varname;
  uint8_t *s = value_str_rw (case_data_rw (outcase, var), len);

  strncpy ((char *) s, str, len);
}


static struct casereader *
preprocess (struct casereader *casereader0, const struct dictionary *dict, void *aux)
{
  struct matrix_format *mformat = aux;
  const struct caseproto *proto = casereader_get_proto (casereader0);
  struct casewriter *writer;
  writer = autopaging_writer_create (proto);

  double *temp_matrix =
    xcalloc (sizeof (*temp_matrix),
	     mformat->n_continuous_vars * mformat->n_continuous_vars);

  /* Make an initial pass to populate our temporary matrix */
  struct casereader *pass0 = casereader_clone (casereader0);
  struct ccase *c;
  int row = (mformat->triangle == LOWER && mformat->diagonal == NO_DIAGONAL) ? 1 : 0;
  for (; (c = casereader_read (pass0)) != NULL; case_unref (c))
    {
      int c_offset = (mformat->triangle == UPPER) ? row : 0;
      if (mformat->triangle == UPPER && mformat->diagonal == NO_DIAGONAL)
	c_offset++;
      const union value *v = case_data (c, mformat->rowtype);
      const char *val = (const char *) value_str (v, 8);
      if (0 == strncasecmp (val, "corr    ", 8) ||
	  0 == strncasecmp (val, "cov     ", 8))
	{
	  int col;
	  for (col = c_offset; col < mformat->n_continuous_vars; ++col)
	    {
	      const struct variable *var =
		dict_get_var (dict,
			      1 + col - c_offset + var_get_dict_index (mformat->varname));

	      double e = case_data (c, var)->f;
	      if (e == SYSMIS)
	      	continue;
	      temp_matrix [col + mformat->n_continuous_vars * row] = e;
	      temp_matrix [row + mformat->n_continuous_vars * col] = e;
	    }
	  row++;
	}
    }
  casereader_destroy (pass0);

  /* Now make a second pass to fill in the other triangle from our
     temporary matrix */
  const int idx = var_get_dict_index (mformat->varname);
  row = 0;
  struct ccase *prev_case = NULL;
  for (; (c = casereader_read (casereader0)) != NULL; prev_case = c)
    {
      case_unref (prev_case);
      struct ccase *outcase = case_create (proto);
      case_copy (outcase, 0, c, 0, caseproto_get_n_widths (proto));
      const union value *v = case_data (c, mformat->rowtype);
      const char *val = (const char *) value_str (v, 8);
      if (0 == strncasecmp (val, "corr    ", 8) ||
	  0 == strncasecmp (val, "cov     ", 8))
	{
	  int col;
	  const struct variable *var = dict_get_var (dict, idx + 1 + row);
	  set_varname_column (outcase, mformat, var_get_name (var), 8);
	  value_copy (case_data_rw (outcase, mformat->rowtype), v, 8);

	  for (col = 0; col < mformat->n_continuous_vars; ++col)
	    {
	      union value *dest_val =
		case_data_rw_idx (outcase,
				  1 + col + var_get_dict_index (mformat->varname));
	      dest_val->f = temp_matrix [col + mformat->n_continuous_vars * row];
	      if (col == row && mformat->diagonal == NO_DIAGONAL)
		dest_val->f = 1.0;
	    }
	  row++;
	}
      else
	{
	  set_varname_column (outcase, mformat, "        ", 8);
	}

      casewriter_write (writer, outcase);
    }

  /* If NODIAGONAL is specified, then a final case must be written */
  if (mformat->diagonal == NO_DIAGONAL)
    {
      int col;
      struct ccase *outcase = case_create (proto);

      if (prev_case)
	case_copy (outcase, 0, prev_case, 0, caseproto_get_n_widths (proto));


      const struct variable *var = dict_get_var (dict, idx + 1 + row);
      set_varname_column (outcase, mformat, var_get_name (var), 8);

      for (col = 0; col < mformat->n_continuous_vars; ++col)
	{
	  union value *dest_val =
	    case_data_rw_idx (outcase, 1 + col +
			      var_get_dict_index (mformat->varname));
	  dest_val->f = temp_matrix [col + mformat->n_continuous_vars * row];
	  if (col == row && mformat->diagonal == NO_DIAGONAL)
	    dest_val->f = 1.0;
	}

      casewriter_write (writer, outcase);
    }

  if (prev_case)
    case_unref (prev_case);

  free (temp_matrix);
  struct casereader *reader1 = casewriter_make_reader (writer);
  casereader_destroy (casereader0);
  return reader1;
}

int
cmd_matrix (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict;
  struct data_parser *parser;
  struct dfm_reader *reader;
  struct file_handle *fh = NULL;
  char *encoding = NULL;
  struct matrix_format mformat;
  int i;
  size_t n_names;
  char **names = NULL;

  mformat.triangle = LOWER;
  mformat.diagonal = DIAGONAL;

  dict = (in_input_program ()
          ? dataset_dict (ds)
          : dict_create (get_default_encoding ()));
  parser = data_parser_create (dict);
  reader = NULL;

  data_parser_set_type (parser, DP_DELIMITED);
  data_parser_set_warn_missing_fields (parser, false);
  data_parser_set_span (parser, false);

  mformat.rowtype = dict_create_var (dict, "ROWTYPE_", 8);
  mformat.varname = dict_create_var (dict, "VARNAME_", 8);

  mformat.n_continuous_vars = 0;

  if (! lex_force_match_id (lexer, "VARIABLES"))
    goto error;

  lex_match (lexer, T_EQUALS);

  if (! parse_mixed_vars (lexer, dict, &names, &n_names, 0))
    {
      int i;
      for (i = 0; i < n_names; ++i)
	free (names[i]);
      free (names);
      goto error;
    }

  for (i = 0; i < n_names; ++i)
    {
      if (0 == strcasecmp (names[i], "ROWTYPE_"))
	{
	  const struct fmt_spec fmt = fmt_for_input (FMT_A, 8, 0);
	  data_parser_add_delimited_field (parser,
					   &fmt,
					   var_get_case_index (mformat.rowtype),
					   "ROWTYPE_");
	}
      else
	{
	  const struct fmt_spec fmt = fmt_for_input (FMT_F, 10, 4);
	  struct variable *v = dict_create_var (dict, names[i], 0);
	  var_set_both_formats (v, &fmt);
	  data_parser_add_delimited_field (parser,
					   &fmt,
					   var_get_case_index (mformat.varname) +
					   ++mformat.n_continuous_vars,
					   names[i]);
	}
    }
  for (i = 0; i < n_names; ++i)
    free (names[i]);
  free (names);

  while (lex_token (lexer) != T_ENDCMD)
    {
      if (! lex_force_match (lexer, T_SLASH))
	goto error;

      if (lex_match_id (lexer, "FORMAT"))
	{
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_SLASH && (lex_token (lexer) != T_ENDCMD))
	    {
	      if (lex_match_id (lexer, "LIST"))
		{
		  data_parser_set_span (parser, false);
		}
	      else if (lex_match_id (lexer, "FREE"))
		{
		  data_parser_set_span (parser, true);
		}
	      else if (lex_match_id (lexer, "UPPER"))
		{
		  mformat.triangle = UPPER;
		}
	      else if (lex_match_id (lexer, "LOWER"))
		{
		  mformat.triangle = LOWER;
		}
	      else if (lex_match_id (lexer, "FULL"))
		{
		  mformat.triangle = FULL;
		}
	      else if (lex_match_id (lexer, "DIAGONAL"))
		{
		  mformat.diagonal = DIAGONAL;
		}
	      else if (lex_match_id (lexer, "NODIAGONAL"))
		{
		  mformat.diagonal = NO_DIAGONAL;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "FILE"))
	{
	  lex_match (lexer, T_EQUALS);
          fh_unref (fh);
	  fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
	  if (fh == NULL)
	    goto error;
	}
      else if (lex_match_id (lexer, "SPLIT"))
	{
	  lex_match (lexer, T_EQUALS);
	  struct variable **split_vars = NULL;
	  size_t n_split_vars;
	  if (! parse_variables (lexer, dict, &split_vars, &n_split_vars, 0))
	    {
	      free (split_vars);
	      goto error;
	    }
	  int i;
	  for (i = 0; i < n_split_vars; ++i)
	    {
	      const struct fmt_spec fmt = fmt_for_input (FMT_F, 4, 0);
	      var_set_both_formats (split_vars[i], &fmt);
	    }
	  dict_reorder_vars (dict, split_vars, n_split_vars);
	  mformat.n_continuous_vars -= n_split_vars;
	  free (split_vars);
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  if (mformat.diagonal == NO_DIAGONAL && mformat.triangle == FULL)
    {
      msg (SE, _("FORMAT = FULL and FORMAT = NODIAGONAL are mutually exclusive."));
      goto error;
    }

  if (fh == NULL)
    fh = fh_inline_file ();
  fh_set_default_handle (fh);

  if (!data_parser_any_fields (parser))
    {
      msg (SE, _("At least one variable must be specified."));
      goto error;
    }

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  reader = dfm_open_reader (fh, lexer, encoding);
  if (reader == NULL)
    goto error;

  if (in_input_program ())
    {
      struct data_list_trns *trns = xmalloc (sizeof *trns);
      trns->parser = parser;
      trns->reader = reader;
      trns->end = NULL;
      add_transformation (ds, data_list_trns_proc, data_list_trns_free, trns);
    }
  else
    {
      data_parser_make_active_file (parser, ds, reader, dict, preprocess, &mformat);
    }

  fh_unref (fh);
  free (encoding);

  return CMD_DATA_LIST;

 error:
  data_parser_destroy (parser);
  if (!in_input_program ())
    dict_destroy (dict);
  fh_unref (fh);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}


/* Input procedure. */

/* Destroys DATA LIST transformation TRNS.
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_trns_free (void *trns_)
{
  struct data_list_trns *trns = trns_;
  data_parser_destroy (trns->parser);
  dfm_close_reader (trns->reader);
  free (trns);
  return true;
}

/* Handle DATA LIST transformation TRNS, parsing data into *C. */
static int
data_list_trns_proc (void *trns_, struct ccase **c, casenumber case_num UNUSED)
{
  struct data_list_trns *trns = trns_;
  int retval;

  *c = case_unshare (*c);
  if (data_parser_parse (trns->parser, trns->reader, *c))
    retval = TRNS_CONTINUE;
  else if (dfm_reader_error (trns->reader) || dfm_eof (trns->reader) > 1)
    {
      /* An I/O error, or encountering end of file for a second
         time, should be escalated into a more serious error. */
      retval = TRNS_ERROR;
    }
  else
    retval = TRNS_END_FILE;

  /* If there was an END subcommand handle it. */
  if (trns->end != NULL)
    {
      double *end = &case_data_rw (*c, trns->end)->f;
      if (retval == TRNS_END_FILE)
        {
          *end = 1.0;
          retval = TRNS_CONTINUE;
        }
      else
        *end = 0.0;
    }

  return retval;
}
