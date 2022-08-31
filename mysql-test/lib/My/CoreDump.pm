# -*- cperl -*-
# Copyright (c) 2008, 2022, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

package My::CoreDump;

use strict;
use Carp;
use File::Temp qw/ tempfile tempdir /;

use My::Platform;
use mtr_results;

# Last resort guess for executable path
my $hint_exe;

# If path in core file is 79 chars we assume it's been truncated.
# Looks like we can still find the full path using 'strings'. If
# that doesn't work, use the hint (mysqld path) as last resort.
sub _verify_binpath {
  my ($binary, $core_name) = @_;

  my $binpath;
  if (length $binary != 79 or -f -x $binary) {
    $binpath = $binary;
    print "Core generated by '$binpath'\n";
  } else {
    if (`strings '$core_name' | grep "unittest/.*-t\$" | tail -1` =~ /(\/.*)/) {
      $binpath= $1;
      print "Guessing that core was generated by a unit test '$binpath'\n";
    } elsif ($::secondary_engine_support) {
      my $exe_name = $ENV{'SECONDARY_ENGINE_SERVER'};
      my $cmd =
        "strings '$core_name' | grep \"/$exe_name\[^/. \]*\$\" | tail -1";
      if (`$cmd` =~ /(\/.*)/) {
        $binpath = $1;
        print "Guessing that core was generated by '$binpath'\n";
      }
    }

    if (not defined $binpath) {
      my $cmd = "strings '$core_name' | grep \"/mysql[^/. ]*\$\" | tail -1";
      if (`$cmd` =~ /(\/.*)/) {
        $binpath = $1;
      }
      if (defined $binpath and -x $binpath) {
        print "Guessing that core was generated by '$binpath'\n";
      } else {
        return unless $hint_exe;
        $binpath = $hint_exe;
        print "Wild guess that core was generated by '$binpath'\n";
      }
    }
  }

  return $binpath;
}

sub _gdb {
  my ($core_name) = @_;

  return unless -f $core_name;

  print "\nTrying 'gdb' to get a backtrace\n";

  # Find out name of binary that generated core
  `gdb -c '$core_name' --batch 2>&1` =~ /Core was generated by `([^\s\'\`]+)/;
  my $binary = $1 or return;
  $binary = _verify_binpath($binary, $core_name) or return;

  # Create tempfile containing gdb commands
  my ($tmp, $tmp_name) = tempfile();
  print $tmp
    "bt\n",
    "thread apply all bt\n",
    "quit\n";
  close $tmp or die "Error closing $tmp_name: $!";

  # Run gdb
  my $gdb_output = `gdb '$binary' -c '$core_name' -x '$tmp_name' --batch 2>&1`;
  unlink $tmp_name or die "Error removing $tmp_name: $!";
  return if $? >> 8;
  return unless $gdb_output;

  resfile_print <<EOF . $gdb_output . "\n";
Output from gdb follows. The first stack trace is from the failing thread.
The following stack traces are from all threads (so the failing one is
duplicated).
--------------------------
EOF
  return 1;
}

sub _dbx {
  my ($core_name) = @_;

  return unless -f $core_name;

  print "\nTrying 'dbx' to get a backtrace\n";

  # Find out name of binary that generated core
  `echo | dbx - '$core_name' 2>&1` =~
    /Corefile specified executable: "([^"]+)"/;
  my $binary = $1 or return;
  $binary = _verify_binpath($binary, $core_name) or return;

  # Find all threads
  my @thr_ids = `echo threads | dbx '$binary' '$core_name' 2>&1` =~ /t@\d+/g;

  # Create tempfile containing dbx commands
  my ($tmp, $tmp_name) = tempfile();
  foreach my $thread (@thr_ids) {
    print $tmp "where $thread\n";
  }
  print $tmp "exit\n";
  close $tmp or die "Error closing $tmp_name: $!";

  # Run dbx
  my $dbx_output = `cat '$tmp_name' | dbx '$binary' '$core_name' 2>&1`;
  unlink $tmp_name or die "Error removing $tmp_name: $!";
  return if $? >> 8;
  return unless $dbx_output;

  resfile_print <<EOF . $dbx_output . "\n";
Output from dbx follows. Stack trace is printed for all threads in order,
above this you should see info about which thread was the failing one.
----------------------------
EOF
  return 1;
}

# The 'cdb debug' prints are added to pinpoint the location of a hang
# which could not be reproduced manually. Will be removed later.
# Check that Debugging tools for Windows are installed
sub cdb_check {
  print localtime() . " cdb debug X\n";

  `cdb -version 2>&1`;
  if ($? >> 8) {
    print "Cannot find cdb. Please Install Debugging tools for Windows\n";
    print "from http://www.microsoft.com/whdc/devtools/debugging/";

    if ($ENV{'ProgramW6432'}) {
      print "install64bit.mspx (native x64 version)\n";
    } else {
      print "installx86.mspx\n";
    }
    return 0;
  } else {
    return 1;
  }
}

sub _cdb {
  my ($core_name) = @_;

  return unless -f $core_name;

  print "\nTrying 'cdb' to get a backtrace\n";

  # Try to set environment for debugging tools for Windows
  if ($ENV{'PATH'} !~ /Debugging Tools/) {
    if ($ENV{'ProgramW6432'}) {
      # On x64 computer
      $ENV{'PATH'} .=
        ";" . $ENV{'ProgramW6432'} . "\\Debugging Tools For Windows (x64)";
      if ($ENV{'WindowsSdkDir'}) {
        $ENV{'PATH'} .= ";" . $ENV{'WindowsSdkDir'} . "\\Debuggers\\x64";
      }
    } else {
      # On x86 computer. Newest versions of Debugging tools are installed
      # in the directory with (x86) suffix, older versions did not have
      # this suffix.
      $ENV{'PATH'} .=
        ";" . $ENV{'ProgramFiles'} . "\\Debugging Tools For Windows (x86)";
      $ENV{'PATH'} .=
        ";" . $ENV{'ProgramFiles'} . "\\Debugging Tools For Windows";

      if ($ENV{'WindowsSdkDir'}) {
        $ENV{'PATH'} .= ";" . $ENV{'WindowsSdkDir'} . "\\Debuggers\\x86";
      }
    }
  }

  # Read module list, find out the name of executable and build symbol
  # path (required by cdb if executable was built on different machine).
  my $tmp_name = $core_name . ".cdb_lmv";
  `cdb -z $core_name -c \"lmv;q\" > $tmp_name 2>&1`;
  print localtime() . " cdb debug C\n";

  if ($? >> 8) {
    unlink($tmp_name);
    # Check if cdb is installed and complain if not
    print localtime() . " cdb debug D\n";
    cdb_check();
    return;
  }
  print localtime() . " cdb debug E\n";

  open(temp, "< $tmp_name");
  my %dirhash = ();
  while (<temp>) {
    if ($_ =~ /Image path\: (.*)/) {
      if (rindex($1, '\\') != -1) {
        my $dir = substr($1, 0, rindex($1, '\\'));
        $dirhash{$dir}++;
      }
    }
  }
  close(temp);
  unlink($tmp_name);
  print localtime() . " cdb debug F\n";
  my $image_path = join(";", (keys %dirhash), ".");

  # For better callstacks, setup _NT_SYMBOL_PATH to include OS symbols.
  # Note : Dowloading symbols for the first time can take some minutes.
  if (!$ENV{'_NT_SYMBOL_PATH'}) {
    my $windir = $ENV{'windir'};
    my $symbol_cache =
      substr($windir, 0, index($windir, '\\')) . "\\cdb_symbols";

    print "OS debug symbols will be downloaded and stored in $symbol_cache.\n";
    print "You can control the location of symbol cache with _NT_SYMBOL_PATH\n";
    print "environment variable. Please refer to Microsoft KB article\n";
    print "http://support.microsoft.com/kb/311503 ";
    print "for details about _NT_SYMBOL_PATH\n";
    print "-----------------------------------------------------------------\n";

    $ENV{'_NT_SYMBOL_PATH'} .=
      "srv*" . $symbol_cache . "*http://msdl.microsoft.com/download/symbols";
  }

  my $symbol_path = $image_path . ";" . $ENV{'_NT_SYMBOL_PATH'};

  # Run cdb. Use "analyze" extension to print crashing thread stacktrace
  # and "uniqstack" to print other threads
  my $cdb_cmd =
"!sym prompts off; !analyze -v; .ecxr; !for_each_frame dv /t;!uniqstack -p;q";
  my $cdb_output =
`cdb -c "$cdb_cmd" -z $core_name -i "$image_path" -y "$symbol_path" -t 0 -lines 2>&1`;
  return if $? >> 8;
  print localtime() . " cdb debug G\n";
  return unless $cdb_output;

  # Remove comments (lines starting with *), stack pointer and frame pointer
  # adresses and offsets to function to make output better readable.
  $cdb_output =~ s/^\*.*\n//gm;
  $cdb_output =~ s/^([\:0-9a-fA-F\`]+ )+//gm;
  $cdb_output =~ s/^ChildEBP RetAddr//gm;
  $cdb_output =~ s/^Child\-SP          RetAddr           Call Site//gm;
  $cdb_output =~ s/\+0x([0-9a-fA-F]+)//gm;

  resfile_print <<EOF . $cdb_output . "\n";
Output from cdb follows. Faulting thread is printed twice,with and without function parameters
Search for STACK_TEXT to see the stack trace of 
the faulting thread. Callstacks of other threads are printed after it.
EOF
  return 1;
}

sub show {
  my ($class, $core_name, $exe_mysqld, $parallel) = @_;
  $hint_exe = $exe_mysqld;

  # On Windows, rely on cdb to be there.
  if (IS_WINDOWS) {
    # Starting cdb is unsafe when used with --parallel > 1 option
    if ($parallel < 2) {
      _cdb($core_name);
    }
    return;
  }

  # We try dbx first; gdb itself may coredump if run on a Sun Studio
  # compiled binary on Solaris.
  my @debuggers = (\&_dbx,
                   \&_gdb,
                   # TODO...
  );

  # Try debuggers until one succeeds
  foreach my $debugger (@debuggers) {
    if ($debugger->($core_name)) {
      return;
    }
  }
  return;
}

1;
