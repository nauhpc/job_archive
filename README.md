# job_archive

Archive Slurm batch scripts as they are submitted as a permanent archive on what was submitted.

## Overview

The job archive process uses POSIX inotify api, monitoring for new files appearing in:
 - /var/spool/slurm/hash.*
 - 1 separate thread per hash directory

Once the jobs show up, then the user's jobs are ultimately copied to:
 - /your-dir/jobscript_archive/userid/year/month/

For example:
 - /your-dir/jobscript_archive/joe/2019/05/

Each job submitted will have two files, a sbatch script and the associated bash env file:
 - 20009123-myjob.sh
 - 20009123-myjob.env

Where 20009123 is the slurm job id.
myjob is from #SBATCH --job-name=myjob 

## Build Instructions

In our environment we are using a default gcc 8.3.1 from Cent OS 8.

To build, we used this simple one liner:

 - g++ job_archive.cpp -o job_archive -std=c++11 -lpthread

## Deployment Environment

In our environment we use a systemd service configured with the following paths:

 - run from: /etc/init.d/jobarchive-service-example

 - program binary run from: /usr/local/jobarchive/job_archive
 
The program writes logging information to STDOUT and so is accessible via
/var/log/messages.

The program writes to the local job archival directory is on the slurm controller host:
 - /var/slurm/jobscript_archive/

Then a cron runs every 5 minutes to move all jobs to a common user accessable path:
 - /your-dir/jobscript_archive
