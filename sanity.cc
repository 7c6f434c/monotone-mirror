// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdio.h>
#include <stdarg.h>

#include <algorithm>
#include <iterator>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>

#include <boost/lexical_cast.hpp>

#include "constants.hh"
#include "platform.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

using namespace std;
using boost::format;

// debugging / logging system

sanity global_sanity;

sanity::sanity() : 
  debug(false), quiet(false), relaxed(false), logbuf(0xffff), 
  already_dumping(false), clean_shutdown(false)
{
  std::string flavour;
  get_system_flavour(flavour);
  L(F("started up on %s\n") % flavour);
}

sanity::~sanity()
{}

void 
sanity::dump_buffer()
{
  if (!filename.empty())
    {
      ofstream out(filename.as_external().c_str());
      if (out)
        {
          copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(out));
          copy(gasp_dump.begin(), gasp_dump.end(), ostream_iterator<char>(out));
          ui.inform((F("wrote debugging log to %s"
                       "if reporting a bug, please include this file")
                     % filename).str());
        }
      else
        ui.inform((F("failed to write debugging log to %s\n") % filename).str());
    }
  else
    ui.inform("discarding debug log (maybe you want --debug or --dump?)");
}

void 
sanity::set_debug()
{
  quiet = false;
  debug = true;

  // it is possible that some pre-setting-of-debug data
  // accumulated in the log buffer (during earlier option processing)
  // so we will dump it now  
  ostringstream oss;
  vector<string> lines;
  copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(oss));
  split_into_lines(oss.str(), lines);
  for (vector<string>::const_iterator i = lines.begin(); i != lines.end(); ++i)
    ui.inform((*i) + "\n");
}

void
sanity::set_brief()
{
  brief = true;
}

void 
sanity::set_quiet()
{
  debug = false;
  quiet = true;
}

void 
sanity::set_relaxed(bool rel)
{
  relaxed = rel;
}

string
sanity::do_format(format const & fmt, char const * file, int line)
{
  try
    {
      return fmt.str();
    }
  catch (std::exception & e)
    {
      ui.inform(F("fatal: formatter failed on %s:%d: %s")
                % file
                % line
                % e.what());
      throw;
    }
}


void 
sanity::log(format const & fmt, 
            char const * file, int line)
{
  string str = do_format(fmt, file, line);
  
  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (debug)
    ui.inform(str);
}

void 
sanity::progress(format const & fmt, 
                 char const * file, int line)
{
  string str = do_format(fmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (! quiet)
    ui.inform(str);
}

void 
sanity::warning(format const & fmt, 
                char const * file, int line)
{
  string str = do_format(fmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  string str2 = "warning: " + str;
  copy(str2.begin(), str2.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (! quiet)
    ui.warn(str);
}

void 
sanity::naughty_failure(string const & expr, format const & explain, 
                        string const & file, int line)
{
  string message;
  log(format("%s:%d: usage constraint '%s' violated\n") % file % line % expr,
      file.c_str(), line);
  prefix_lines_with(_("misuse: "), explain.str(), message);
  gasp();
  throw informative_failure(message);
}

void 
sanity::error_failure(string const & expr, format const & explain, 
                        string const & file, int line)
{
  string message;
  log(format("%s:%d: detected error '%s' violated\n") % file % line % expr,
      file.c_str(), line);
  prefix_lines_with(_("error: "), explain.str(), message);
  throw informative_failure(message);
}

void 
sanity::invariant_failure(string const & expr, 
                          string const & file, int line)
{
  format fmt = 
    format("%s:%d: invariant '%s' violated\n") 
    % file % line % expr;
  log(fmt, file.c_str(), line);
  gasp();
  throw logic_error(fmt.str());
}

void 
sanity::index_failure(string const & vec_expr, 
                      string const & idx_expr, 
                      unsigned long sz,
                      unsigned long idx,
                      string const & file, int line)
{
  format fmt = 
    format("%s:%d: index '%s' = %d overflowed vector '%s' with size %d\n")
    % file % line % idx_expr % idx % vec_expr % sz;
  log(fmt, file.c_str(), line);
  gasp();
  throw logic_error(fmt.str());
}

// Last gasp dumps

void
sanity::gasp()
{
  if (already_dumping)
    {
      L(F("ignoring request to give last gasp; already in process of dumping\n"));
      return;
    }
  already_dumping = true;
  L(F("saving current work set: %i items") % musings.size());
  std::ostringstream out;
  out << F("Current work set: %i items\n") % musings.size();
  for (std::vector<MusingI const *>::const_iterator
         i = musings.begin(); i != musings.end(); ++i)
    {
      std::string tmp;
      try
        {
          (*i)->gasp(tmp);
          out << tmp;
        }
      catch (logic_error)
        {
          out << tmp;
          out << "<caught logic_error>\n";
          L(F("ignoring error trigged by saving work set to debug log"));
        }
      catch (informative_failure)
        {
          out << tmp;
          out << "<caught informative_failure>\n";
          L(F("ignoring error trigged by saving work set to debug log"));
        }
    }
  gasp_dump = out.str();
  L(F("finished saving work set"));
  if (debug)
    {
      ui.inform("contents of work set:");
      ui.inform(gasp_dump);
    }
  already_dumping = false;
}

MusingI::MusingI()
{
  if (!global_sanity.already_dumping)
    global_sanity.musings.push_back(this);
}

MusingI::~MusingI()
{
  if (!global_sanity.already_dumping)
    {
      I(global_sanity.musings.back() == this);
      global_sanity.musings.pop_back();
    }
}

void
dump(std::string const & obj, std::string & out)
{
  out = obj;
}


void MusingBase::gasp(const std::string & objstr, std::string & out) const
{
  out = (boost::format("----- begin '%s' (in %s, at %s:%d)\n"
                       "%s"
                       "-----   end '%s' (in %s, at %s:%d)\n")
         % name % func % file % line
         % objstr
         % name % func % file % line
         ).str();
}


boost::format F(const char * str)
{
  return boost::format(gettext(str), get_user_locale());
}


boost::format FP(const char * str1, const char * strn, unsigned long count)
{
  return boost::format(ngettext(str1, strn, count), get_user_locale());
}

