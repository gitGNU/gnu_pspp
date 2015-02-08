/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009, 2011, 2012 Free Software Foundation, Inc.

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

#include "math/histogram.h"
#include "math/decimal.h"

#include <gsl/gsl_histogram.h>
#include <math.h>

#include "data/settings.h"
#include "libpspp/message.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "math/chart-geometry.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "gl/xalloc.h"

void
histogram_add (struct histogram *h, double y, double c)
{
  struct statistic *stat = &h->parent;
  stat->accumulate (stat, NULL, c, 0, y);
}

static void
acc (struct statistic *s, const struct ccase *cx UNUSED, double c, double cc UNUSED, double y)
{
  struct histogram *hist = UP_CAST (s, struct histogram, parent);

  gsl_histogram_accumulate (hist->gsl_hist, y, c);
}

static void
destroy (struct statistic *s)
{
  struct histogram *h = UP_CAST (s, struct histogram, parent);
  gsl_histogram_free (h->gsl_hist);
  free (s);
}


static
double get_slack (double limit, double half_bin_width, int *n_half_bins)
{
  double ipart, remainder;

  assert (half_bin_width > 0);

  remainder =  modf (limit / half_bin_width, &ipart);

  /* In C modf and % behave in an unexpected (to me at any rate) manner
     when presented with a negative value

     For example, modf (-7.0 / 3.0) returns -2.0 R -0.3333
  */

  
  *n_half_bins = ipart;

  return remainder * half_bin_width;
}


/* This functions adjusts the upper and lower range of the histogram to make them fit BIN_WIDTH
   MIN and MAX are the lowest and highest data to be plotted in the histogram.
   ADJ_MIN and ADJ_MAX are locations of the adjusted values of MIN and MAX (the range will
   always be  equal or slightly larger).
   Returns the number of bins.

   The "testing_assert" expressions in this function should be algebraically correct.
   However, due to floating point rounding they could fail, especially when small numbers
   are involved.  In normal use, therefore, testing_assert does nothing.
 */
static int
adjust_bin_ranges (double bin_width, double min, double max, double *adj_min, double *adj_max)
{
  const double half_bin_width = bin_width / 2.0;

  /* The lower and upper limits of the histogram, in units of half
     bin widths */
  int lower_limit, upper_limit;

  double lower_slack =  get_slack (min, half_bin_width, &lower_limit);
  double upper_slack = -get_slack (max, half_bin_width, &upper_limit);

  testing_assert (max > min);

  /* If min is negative, then lower_slack may be less than zero.
     In this case, the lower bound must be extended in the negative direction
     so that it is less than OR EQUAL to min.
   */
  if (lower_slack < 0)
    {
      lower_limit--;
      lower_slack += half_bin_width;
    }
  testing_assert (lower_limit * half_bin_width <= min);

  /* However, the upper bound must be extended regardless, because histogram bins
     span the range [lower, upper). In other words, the upper bound must be
     greater than max.
  */
  upper_limit++;;
  upper_slack += half_bin_width;
  testing_assert (upper_limit * half_bin_width > max);

  /* The range must be an EVEN number of half bin_widths */
  if ( (upper_limit - lower_limit) % 2)
    {
      /* Extend the range at the end which gives the least unused space */
      if (upper_slack > lower_slack)
        {
          lower_limit--;
          lower_slack += half_bin_width;
        }
      else
        {
          upper_limit++;
          upper_slack += half_bin_width;
        }
    }

  /* But the range should be aligned to an ODD number of
     half bin widths, so that the labels are aesthetically pleasing ones.
     Otherwise we are likely to get labels such as -3 -1 1 3 instead of -2 0 2 4
  */
  if ( lower_limit % 2 == 0)
    {
      /* If there is not enough slack at either end to perform a shift,
         then we must extend the range so that there is.  We must extend
         by two half bin widths in order to preserve the EVEN condition
         established above.  Also, we extend on the end with the least
         slack, in order to keep things as balanced as possible. */
      if ( upper_slack > lower_slack && upper_slack <= half_bin_width)
        {
          lower_limit -= 2;
          lower_slack += 2 * half_bin_width;
        }
           
      if (lower_slack > upper_slack && lower_slack < half_bin_width)
        {
          upper_limit += 2;
          upper_slack += 2 * half_bin_width;
        }

      if (upper_slack > lower_slack)
        {
          testing_assert (upper_slack > half_bin_width);

          /* Adjust the range to the left */
          lower_limit --;
          upper_limit --;
          upper_slack -= half_bin_width;
          lower_slack += half_bin_width;
        }
      else
        {
          testing_assert (lower_slack >= half_bin_width);

          /* Adjust the range to the right */
          lower_limit ++;
          upper_limit ++;
          lower_slack -= half_bin_width;
          upper_slack += half_bin_width;
        }
    }

  /* If there are any completely empty bins, then remove them,
     since empty bins don't really add much information to the histogram.
   */
  if (upper_slack > 2 * half_bin_width)
    {
      upper_slack -= 2 * half_bin_width;
      upper_limit -=2;
    }

  if (lower_slack >= 2 * half_bin_width)
    {
      lower_slack -= 2 * half_bin_width;
      lower_limit +=2;
    }

  *adj_min = lower_limit * half_bin_width;
  *adj_max = upper_limit * half_bin_width;

  testing_assert (*adj_max > max);
  testing_assert (*adj_min <= min);

  return (upper_limit - lower_limit) / 2.0;
}



struct histogram *
histogram_create (double bin_width_in, double min, double max)
{
  struct decimal bin_width;
  const int MAX_BINS = 25;
  struct histogram *h;
  struct statistic *stat;
  int bins;
  double adjusted_min, adjusted_max;

  if (max == min)
    {
      msg (MW, _("Not creating histogram because the data contains less than 2 distinct values"));
      return NULL;
    }

  assert (bin_width_in > 0);

  chart_rounded_tick (bin_width_in, &bin_width);
  bins = adjust_bin_ranges (decimal_to_double (&bin_width), 
			    min, max, &adjusted_min, &adjusted_max);

  /* Force the number of bins to lie in a sensible range. */
  if (bins > MAX_BINS) 
    {
      chart_rounded_tick ((max - min) / (double) (MAX_BINS - 1), &bin_width);
      bins = adjust_bin_ranges (decimal_to_double (&bin_width),
                                min, max, &adjusted_min, &adjusted_max);
    }

  /* Can this ever happen? */
  if (bins < 1)
    bins = 1;

  h = xmalloc (sizeof *h);

  h->gsl_hist = gsl_histogram_alloc (bins);

  gsl_histogram_set_ranges_uniform (h->gsl_hist, adjusted_min, adjusted_max);

  stat = &h->parent;
  stat->accumulate = acc;
  stat->destroy = destroy;

  return h;
}

