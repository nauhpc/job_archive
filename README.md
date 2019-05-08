# job_archive

Archive SLURM batch jobs as they are submitted as a permanent archive on what was run

## Overview

The job archive uses POSIX inotify api, which is camped on this directory waiting for jobs to be submitted:
 - /var/spool/slurm/hash.*

Once the jobs show up, then the user's jobs are ultimately copied to:
 - /your-dir/jobscript_archive/<userid>/<year>/<month>/

For example:
 - /your-dir/jobscript_archive/joe/2019/05/

Each job submitted will have two files, a sbatch script and the associated bash env file:
 - 20009123-myjob.sh
 - 20009123-myjob.env

Where 20009123 is the slurm job id.
myjob is from #SBATCH --job-name=myjob 

## Build Instructions

In our environment we are using a default gcc 4.4.7 from Cent OS 6.

To build, use this simple one liner:

 - g++ job_archive.cpp -o job_archive -std=c++0x -lpthread

## Deployment Environment

In our environment we use an init.d service configured with the following paths:

 - run from: /etc/init.d/jobarchive-service-example

 - program binary run from: /usr/local/jobarchive/job_archive

 - internal logging at: /var/log/jobarchive/jobarchive.log

The program writes to the local job archival directory is on the slurm controller host:
 - /var/slurm/jobscript_archive/

Then a cron runs every 5 minutes to move all jobs to a common user accessable path:
 - /your-dir/jobscript_archive


