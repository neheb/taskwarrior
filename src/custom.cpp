////////////////////////////////////////////////////////////////////////////////
// task - a command line task list manager.
//
// Copyright 2006 - 2010, Paul Beckingham.
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the
//
//     Free Software Foundation, Inc.,
//     51 Franklin Street, Fifth Floor,
//     Boston, MA
//     02110-1301
//     USA
//
////////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <time.h>

#include "Context.h"
#include "Date.h"
#include "Table.h"
#include "text.h"
#include "util.h"
#include "main.h"

#ifdef HAVE_LIBNCURSES
#include <ncurses.h>
#endif

extern Context context;
static std::vector <std::string> customReports;

////////////////////////////////////////////////////////////////////////////////
// This report will eventually become the one report that many others morph into
// via the .taskrc file.
int handleCustomReport (const std::string& report, std::string &outs)
{
  // Load report configuration.
  std::string columnList = context.config.get ("report." + report + ".columns");
  std::string labelList  = context.config.get ("report." + report + ".labels");
  std::string sortList   = context.config.get ("report." + report + ".sort");
  std::string filterList = context.config.get ("report." + report + ".filter");

  std::vector <std::string> filterArgs;
  split (filterArgs, filterList, ' ');
  {
    Cmd cmd (report);
    Task task;
    Sequence sequence;
    Subst subst;
    Filter filter;
    context.parse (filterArgs, cmd, task, sequence, subst, filter);

    context.sequence.combine (sequence);

    // Allow limit to be overridden by the command line.
    if (!context.task.has ("limit") && task.has ("limit"))
      context.task.set ("limit", task.get ("limit"));

    foreach (att, filter)
      context.filter.push_back (*att);
  }

  // Get all the tasks.
  std::vector <Task> tasks;
  context.tdb.lock (context.config.getBoolean ("locking"));
  handleRecurrence ();
  context.tdb.load (tasks, context.filter);
  context.tdb.commit ();
  context.tdb.unlock ();

  return runCustomReport (
    report,
    columnList,
    labelList,
    sortList,
    filterList,
    tasks,
    outs);
}

////////////////////////////////////////////////////////////////////////////////
// This report will eventually become the one report that many others morph into
// via the .taskrc file.
int runCustomReport (
  const std::string& report,
  const std::string& columnList,
  const std::string& labelList,
  const std::string& sortList,
  const std::string& filterList,
  std::vector <Task>& tasks,
  std::string &outs)
{
  int rc = 0;
  // Load report configuration.
  std::vector <std::string> columns;
  split (columns, columnList, ',');
  validReportColumns (columns);

  std::vector <std::string> labels;
  split (labels, labelList, ',');

  if (columns.size () != labels.size () && labels.size () != 0)
    throw std::string ("There are a different number of columns than labels ") +
          "for report '" + report + "'.";

  std::map <std::string, std::string> columnLabels;
  if (labels.size ())
    for (unsigned int i = 0; i < columns.size (); ++i)
      columnLabels[columns[i]] = labels[i];

  std::vector <std::string> sortOrder;
  split (sortOrder, sortList, ',');
  validSortColumns (columns, sortOrder);

  std::vector <std::string> filterArgs;
  split (filterArgs, filterList, ' ');
  {
    Cmd cmd (report);
    Task task;
    Sequence sequence;
    Subst subst;
    Filter filter;
    context.parse (filterArgs, cmd, task, sequence, subst, filter);

    context.sequence.combine (sequence);

    // Allow limit to be overridden by the command line.
    if (!context.task.has ("limit") && task.has ("limit"))
      context.task.set ("limit", task.get ("limit"));

    foreach (att, filter)
      context.filter.push_back (*att);
  }

  // Filter sequence.
  if (context.sequence.size ())
    context.filter.applySequence (tasks, context.sequence);

  // Initialize colorization for subsequent auto colorization.
  initializeColorRules ();

  Table table;
  table.setTableWidth (context.getWidth ());
  table.setDateFormat (context.config.get ("dateformat"));
  table.setReportName (report);

  foreach (task, tasks)
    table.addRow ();

  int columnCount = 0;
  int dueColumn = -1;
  foreach (col, columns)
  {
    // Add each column individually.
    if (*col == "id")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "ID");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      int row = 0;
      foreach (task, tasks)
        if (task->id != 0)
          table.addCell (row++, columnCount, task->id);
        else
          table.addCell (row++, columnCount, "-");
    }

    else if (*col == "uuid")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "UUID");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      foreach (task, tasks)
        table.addCell (row++, columnCount, task->get ("uuid"));
    }

    else if (*col == "project")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Project");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      foreach (task, tasks)
        table.addCell (row++, columnCount, task->get ("project"));
    }

    else if (*col == "priority")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Pri");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      foreach (task, tasks)
        table.addCell (row++, columnCount, task->get ("priority"));
    }

    else if (*col == "priority_long")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Pri");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      std::string pri;
      foreach (task, tasks)
      {
        pri = task->get ("priority");

             if (pri == "H") pri = "High";   // TODO i18n
        else if (pri == "M") pri = "Medium"; // TODO i18n
        else if (pri == "L") pri = "Low";    // TODO i18n

        table.addCell (row++, columnCount, pri);
      }
    }

    else if (*col == "entry")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Added");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string entered;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        entered = tasks[row].get ("entry");
        if (entered.length ())
        {
          Date dt (::atoi (entered.c_str ()));
          entered = dt.toString (context.config.get ("dateformat"));
          table.addCell (row, columnCount, entered);
        }
      }
    }

    else if (*col == "entry_time")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Added");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string entered;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        entered = tasks[row].get ("entry");
        if (entered.length ())
        {
          Date dt (::atoi (entered.c_str ()));
          entered = dt.toStringWithTime (context.config.get ("dateformat"));
          table.addCell (row, columnCount, entered);
        }
      }
    }

    else if (*col == "start")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Started");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string started;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        started = tasks[row].get ("start");
        if (started.length ())
        {
          Date dt (::atoi (started.c_str ()));
          started = dt.toString (context.config.get ("dateformat"));
          table.addCell (row, columnCount, started);
        }
      }
    }

    else if (*col == "start_time")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Started");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string started;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        started = tasks[row].get ("start");
        if (started.length ())
        {
          Date dt (::atoi (started.c_str ()));
          started = dt.toStringWithTime (context.config.get ("dateformat"));
          table.addCell (row, columnCount, started);
        }
      }
    }

    else if (*col == "end")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Completed");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string started;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        started = tasks[row].get ("end");
        if (started.length ())
        {
          Date dt (::atoi (started.c_str ()));
          started = dt.toString (context.config.get ("dateformat"));
          table.addCell (row, columnCount, started);
        }
      }
    }

    else if (*col == "end_time")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Completed");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string format = context.config.get ("dateformat");

      std::string started;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        started = tasks[row].get ("end");
        if (started.length ())
        {
          Date dt (::atoi (started.c_str ()));
          started = dt.toStringWithTime (format);
          table.addCell (row, columnCount, started);
        }
      }
    }

    else if (*col == "due")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Due");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      std::string format = context.config.get ("report." + report + ".dateformat");
      if (format == "")
        format = context.config.get ("dateformat.report");
      if (format == "")
        format = context.config.get ("dateformat");

      int row = 0;
      std::string due;
      foreach (task, tasks)
        table.addCell (row++, columnCount, getDueDate (*task, format));

      dueColumn = columnCount;
    }

    else if (*col == "age")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Age");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string created;
      std::string age;
      Date now;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        created = tasks[row].get ("entry");
        if (created.length ())
        {
          Date dt (::atoi (created.c_str ()));
          age = formatSeconds ((time_t) (now - dt));
          table.addCell (row, columnCount, age);
        }
      }
    }

    else if (*col == "age_compact")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Age");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      std::string created;
      std::string age;
      Date now;
      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        created = tasks[row].get ("entry");
        if (created.length ())
        {
          Date dt (::atoi (created.c_str ()));
          age = formatSecondsCompact ((time_t) (now - dt));
          table.addCell (row, columnCount, age);
        }
      }
    }

    else if (*col == "active")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Active");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      for (unsigned int row = 0; row < tasks.size(); ++row)
        if (tasks[row].has ("start"))
          table.addCell (row, columnCount, "*");
    }

    else if (*col == "tags")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Tags");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      std::vector <std::string> all;
      std::string tags;
      foreach (task, tasks)
      {
        task->getTags (all);
        join (tags, " ", all);
        table.addCell (row++, columnCount, tags);
      }
    }

    else if (*col == "description_only")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Description");
      table.setColumnWidth (columnCount, Table::flexible);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      foreach (task, tasks)
        table.addCell (row++, columnCount, task->get ("description"));
    }

    else if (*col == "description")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Description");
      table.setColumnWidth (columnCount, Table::flexible);
      table.setColumnJustification (columnCount, Table::left);

      int row = 0;
      foreach (task, tasks)
        table.addCell (row++, columnCount, getFullDescription (*task, report));
    }

    else if (*col == "recur")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Recur");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      for (unsigned int row = 0; row < tasks.size(); ++row)
      {
        std::string recur = tasks[row].get ("recur");
        if (recur != "")
          table.addCell (row, columnCount, recur);
      }
    }

    else if (*col == "recurrence_indicator")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "R");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      for (unsigned int row = 0; row < tasks.size(); ++row)
        if (tasks[row].has ("recur"))
          table.addCell (row, columnCount, "R");
    }

    else if (*col == "tag_indicator")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "T");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      for (unsigned int row = 0; row < tasks.size(); ++row)
        if (tasks[row].getTagCount ())
          table.addCell (row, columnCount, "+");
    }

    else if (*col == "wait")
    {
      table.addColumn (columnLabels[*col] != "" ? columnLabels[*col] : "Wait");
      table.setColumnWidth (columnCount, Table::minimum);
      table.setColumnJustification (columnCount, Table::right);

      int row = 0;
      std::string wait;
      foreach (task, tasks)
      {
        wait = task->get ("wait");
        if (wait != "")
        {
          Date dt (::atoi (wait.c_str ()));
          wait = dt.toString (context.config.get ("dateformat"));
          table.addCell (row++, columnCount, wait);
        }
      }
    }

    // Common to all columns.
    // Add underline.
    if ((context.config.getBoolean ("color") || context.config.getBoolean ("_forcecolor")) &&
        context.config.getBoolean ("fontunderline"))
      table.setColumnUnderline (columnCount);
    else
      table.setTableDashedUnderline ();

    ++columnCount;
  }

  // Dynamically add sort criteria.
  // Build a map of column names -> index.
  std::map <std::string, unsigned int> columnIndex;
  for (unsigned int c = 0; c < columns.size (); ++c)
    columnIndex[columns[c]] = c;

  foreach (sortColumn, sortOrder)
  {
    // Separate column and direction.
    std::string column = sortColumn->substr (0, sortColumn->length () - 1);
    char direction = (*sortColumn)[sortColumn->length () - 1];

    // TODO This code should really be using Att::type.
    if (column == "id")
      table.sortOn (columnIndex[column],
                    (direction == '+' ?
                      Table::ascendingNumeric :
                      Table::descendingNumeric));

    else if (column == "priority")
      table.sortOn (columnIndex[column],
                    (direction == '+' ?
                      Table::ascendingPriority :
                      Table::descendingPriority));

    else if (column == "entry" || column == "start" || column == "wait" ||
             column == "until" || column == "end")
      table.sortOn (columnIndex[column],
                    (direction == '+' ?
                      Table::ascendingDate :
                      Table::descendingDate));

    else if (column == "due")
      table.sortOn (columnIndex[column],
                    (direction == '+' ?
                      Table::ascendingDueDate :
                      Table::descendingDueDate));

    else if (column == "recur")
      table.sortOn (columnIndex[column],
                    (direction == '+' ?
                      Table::ascendingPeriod :
                      Table::descendingPeriod));

    else
      table.sortOn (columnIndex[column],
                    (direction == '+' ?
                      Table::ascendingCharacter :
                      Table::descendingCharacter));
  }

  // Now auto colorize all rows.
  std::string due;
  Color color_due     (context.config.get ("color.due"));
  Color color_overdue (context.config.get ("color.overdue"));

  bool imminent;
  bool overdue;
  for (unsigned int row = 0; row < tasks.size (); ++row)
  {
    imminent = false;
    overdue  = false;
    due = tasks[row].get ("due");
    if (due.length ())
    {
      switch (getDueState (due))
      {
      case 2: overdue  = true; break;
      case 1: imminent = true; break;
      case 0:
      default:                 break;
      }
    }

    if (context.config.getBoolean ("color") || context.config.getBoolean ("_forcecolor"))
    {
      Color c (tasks[row].get ("fg") + " " + tasks[row].get ("bg"));
      autoColorize (tasks[row], c);
      table.setRowColor (row, c);

      if (dueColumn != -1)
      {
        c.blend (overdue ? color_overdue : color_due);
        table.setCellColor (row, columnCount, c);
      }
    }
  }

  // If an alternating row color is specified, notify the table.
  if (context.config.getBoolean ("color") || context.config.getBoolean ("_forcecolor"))
  {
    Color alternate (context.config.get ("color.alternate"));
    if (alternate.nontrivial ())
      table.setTableAlternateColor (alternate);
  }

  // Limit the number of rows according to the report definition.
  int maximum = context.config.getInteger (std::string ("report.") + report + ".limit");

  // If the custom report has a defined limit, then allow a numeric override.
  // This is an integer specified as a filter (limit:10).
  if (context.task.has ("limit"))
    maximum = atoi (context.task.get ("limit").c_str ());

  std::stringstream out;
  if (table.rowCount ())
    out << optionalBlankLine ()
        << table.render (maximum)
        << optionalBlankLine ()
        << table.rowCount ()
        << (table.rowCount () == 1 ? " task" : " tasks")
        << std::endl;
  else {
    out << "No matches."
        << std::endl;
    rc = 1;
  }

  outs = out.str ();
  return rc;
}

////////////////////////////////////////////////////////////////////////////////
void validReportColumns (const std::vector <std::string>& columns)
{
  std::vector <std::string> bad;

  std::vector <std::string>::const_iterator it;
  for (it = columns.begin (); it != columns.end (); ++it)
    if (*it != "id"                   &&
        *it != "uuid"                 &&
        *it != "project"              &&
        *it != "priority"             &&
        *it != "priority_long"        &&
        *it != "entry"                &&
        *it != "entry_time"           &&
        *it != "start"                &&
        *it != "start_time"           &&
        *it != "end"                  &&
        *it != "end_time"             &&
        *it != "due"                  &&
        *it != "age"                  &&
        *it != "age_compact"          &&
        *it != "active"               &&
        *it != "tags"                 &&
        *it != "recur"                &&
        *it != "recurrence_indicator" &&
        *it != "tag_indicator"        &&
        *it != "description_only"     &&
        *it != "description"          &&
        *it != "wait")
      bad.push_back (*it);

  if (bad.size ())
  {
    std::string error;
    join (error, ", ", bad);
    throw std::string ("Unrecognized column name: ") + error;
  }
}

////////////////////////////////////////////////////////////////////////////////
void validSortColumns (
  const std::vector <std::string>& columns,
  const std::vector <std::string>& sortColumns)
{
  std::vector <std::string> bad;
  std::vector <std::string>::const_iterator sc;
  for (sc = sortColumns.begin (); sc != sortColumns.end (); ++sc)
  {
    std::vector <std::string>::const_iterator co;
    for (co = columns.begin (); co != columns.end (); ++co)
      if (sc->substr (0, sc->length () - 1) == *co)
        break;

    if (co == columns.end ())
      bad.push_back (*sc);
  }

  if (bad.size ())
  {
    std::string error;
    join (error, ", ", bad);
    throw std::string ("Sort column is not part of the report: ") + error;
  }
}

////////////////////////////////////////////////////////////////////////////////
