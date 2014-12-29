/* PSPP - a program for statistical analysis.
   Copyright (C) 2013, 2014 Free Software Foundation, Inc.

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

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "data/any-reader.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/csv-file-writer.h"
#include "data/file-name.h"
#include "data/por-file-writer.h"
#include "data/settings.h"
#include "data/sys-file-encryption.h"
#include "data/sys-file-writer.h"
#include "data/file-handle-def.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"

#include "gl/error.h"
#include "gl/getpass.h"
#include "gl/progname.h"
#include "gl/version-etc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static void usage (void);

static void decrypt_sav_file (struct encrypted_sys_file *enc,
                              const char *input_filename,
                              const char *output_filename,
                              const char *password);

int
main (int argc, char *argv[])
{
  const char *input_filename;
  const char *output_filename;

  long long int max_cases = LLONG_MAX;
  struct dictionary *dict;
  struct casereader *reader;
  struct file_handle *input_fh;
  const char *encoding = NULL;
  struct encrypted_sys_file *enc;

  const char *output_format = NULL;
  struct file_handle *output_fh;
  struct casewriter *writer;
  const char *password = NULL;

  long long int i;

  set_program_name (argv[0]);
  i18n_init ();
  fh_init ();
  settings_init ();

  for (;;)
    {
      static const struct option long_options[] =
        {
          { "cases",    required_argument, NULL, 'c' },
          { "encoding", required_argument, NULL, 'e' },
          { "password", required_argument, NULL, 'p' },

          { "output-format", required_argument, NULL, 'O' },

          { "help",    no_argument,       NULL, 'h' },
          { "version", no_argument,       NULL, 'v' },
          { NULL,      0,                 NULL, 0 },
        };

      int c;

      c = getopt_long (argc, argv, "c:e:p:O:hv", long_options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'c':
          max_cases = strtoull (optarg, NULL, 0);
          break;

        case 'e':
          encoding = optarg;
          break;

        case 'p':
          password = optarg;
          break;

        case 'O':
          output_format = optarg;
          break;

        case 'v':
          version_etc (stdout, "pspp-convert", PACKAGE_NAME, PACKAGE_VERSION,
                       "Ben Pfaff", "John Darrington", NULL_SENTINEL);
          exit (EXIT_SUCCESS);

        case 'h':
          usage ();
          exit (EXIT_SUCCESS);

        default:
          exit (EXIT_FAILURE);
        }
    }

  if (optind + 2 != argc)
    error (1, 0, _("exactly two non-option arguments are required; "
                   "use --help for help"));

  input_filename = argv[optind];
  output_filename = argv[optind + 1];
  if (output_format == NULL)
    {
      const char *dot = strrchr (output_filename, '.');
      if (dot == NULL)
        error (1, 0, _("%s: cannot guess output format (use -O option)"),
               output_filename);

      output_format = dot + 1;
    }

  if (encrypted_sys_file_open (&enc, input_filename) > 0)
    {
      if (strcmp (output_format, "sav") && strcmp (output_format, "sys"))
        error (1, 0, _("can only convert encrypted data file to sav or sys "
                       "format"));

      decrypt_sav_file (enc, input_filename, output_filename, password);
      goto exit;
    }

  input_fh = fh_create_file (NULL, input_filename, fh_default_properties ());
  reader = any_reader_open_and_decode (input_fh, encoding, &dict, NULL);
  if (reader == NULL)
    exit (1);

  output_fh = fh_create_file (NULL, output_filename, fh_default_properties ());
  if (!strcmp (output_format, "csv") || !strcmp (output_format, "txt"))
    {
      struct csv_writer_options options;

      csv_writer_options_init (&options);
      options.include_var_names = true;
      writer = csv_writer_open (output_fh, dict, &options);
    }
  else if (!strcmp (output_format, "sav") || !strcmp (output_format, "sys"))
    {
      struct sfm_write_options options;

      options = sfm_writer_default_options ();
      writer = sfm_open_writer (output_fh, dict, options);
    }
  else if (!strcmp (output_format, "por"))
    {
      struct pfm_write_options options;

      options = pfm_writer_default_options ();
      writer = pfm_open_writer (output_fh, dict, options);
    }
  else
    {
      error (1, 0, _("%s: unknown output format (use -O option)"),
             output_filename);
      NOT_REACHED ();
    }

  for (i = 0; i < max_cases; i++)
    {
      struct ccase *c;

      c = casereader_read (reader);
      if (c == NULL)
        break;

      casewriter_write (writer, c);
    }

  if (!casereader_destroy (reader))
    error (1, 0, _("%s: error reading input file"), input_filename);
  if (!casewriter_destroy (writer))
    error (1, 0, _("%s: error writing output file"), output_filename);

exit:
  fh_done ();
  i18n_done ();

  return 0;
}

static void
decrypt_sav_file (struct encrypted_sys_file *enc,
                  const char *input_filename,
                  const char *output_filename,
                  const char *password)
{
  FILE *out;
  int err;

  if (password == NULL)
    {
      password = getpass ("password: ");
      if (password == NULL)
        exit (1);
    }

  if (!encrypted_sys_file_unlock (enc, password))
    error (1, 0, _("sorry, wrong password"));

  out = fn_open (output_filename, "wb");
  if (out == NULL)
    error (1, errno, ("%s: error opening output file"), output_filename);

  for (;;)
    {
      uint8_t buffer[1024];
      size_t n;

      n = encrypted_sys_file_read (enc, buffer, sizeof buffer);
      if (n == 0)
        break;

      if (fwrite (buffer, 1, n, out) != n)
        error (1, errno, ("%s: write error"), output_filename);
    }

  err = encrypted_sys_file_close (enc);
  if (err)
    error (1, err, ("%s: read error"), input_filename);

  if (fflush (out) == EOF)
    error (1, errno, ("%s: write error"), output_filename);
  fn_close (output_filename, out);
}

static void
usage (void)
{
  printf ("\
%s, a utility for converting SPSS data files to other formats.\n\
Usage: %s [OPTION]... INPUT OUTPUT\n\
where INPUT is an SPSS system or portable file\n\
  and OUTPUT is the name of the desired output file.\n\
\n\
The desired format of OUTPUT is by default inferred from its extension:\n\
  csv txt             comma-separated value\n\
  sav sys             SPSS system file\n\
  por                 SPSS portable file\n\
\n\
Options:\n\
  -O, --output-format=FORMAT  set specific output format, where FORMAT\n\
                      is one of the extensions listed above\n\
  -e, --encoding=CHARSET  override encoding of input data file\n\
  -c MAXCASES         limit number of cases to copy (default is all cases)\n\
  -p PASSWORD         password for encrypted .sav files\n\
  --help              display this help and exit\n\
  --version           output version information and exit\n",
          program_name, program_name);
}
