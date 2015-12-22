# ==== Purpose ====
#
# Make several attempts checking if the specified file has been created
# or until timeout occurs
#
# ==== Usage ====
#
# --let $file_being_created= FILE_PATH
# --source include/wait_for_file_to_be_created.inc
#
# Parameters:
#   $file_being_created
#     The path to a file which is expected to be created soon.

--let FILE_PATH=$file_being_created
perl;
  use strict;
  use warnings;

  my $output = $ENV{'STATUS'};
    
  my $local_file_path= $ENV{'FILE_PATH'};
  my $counter = 10;
  while(($counter != 0) && !(-f $local_file_path))
  {
    sleep(1);
    --$counter;
  }
  die "Waited too long for file to be created" if $counter == 0;
EOF

