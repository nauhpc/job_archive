# job_archive

Archive Slurm job scripts as they are submitted to a permanent archive, with no added latency.

## Overview

The job archive uses POSIX inotify api with one thread for each of the hash directories located on:
 - /var/spool/slurm/hash.*

Once the job scripts show up under the hash directories the .sh and .env files are copied to:
 - /var/slurm/jobscript_archive/userid/year/month/

For example:
 - /var/slurm/jobscript_archive/joe/2019/05/

Each job submitted will have two files, a sbatch script and the associated bash env file:
 - 20009123-myjob.sh
 - 20009123-myjob.env

Where 20009123 is the slurm job id.
myjob is from #SBATCH --job-name=myjob

Note that only jobs submitted via sbatch will have a job_script and an environment variable that can be backed up.

## Build Instructions

In our environment we are using a default gcc 8.3.1 from Cent OS 8.

To build, we used this simple one liner:

 - g++ job\_archive.cpp -o job\_archive -lpthread
 - Note: with an older compiler you'd need the -std=c++11 option

## Deployment Environment

In our environment we use a systemd service configured with the following paths:

 - run from: /etc/init.d/jobarchive-service-example
 - program binary run from: /usr/local/jobarchive/job_archive
 
Here's our systemd service file as an example:	

```
[Unit]
Description=Slurm Jobscript Archiver
After=network.target remote-fs.target

[Service]
User=slurm
ExecStart=/usr/local/jobarchive/jobarchive
ExecReload=/bin/kill -HUP $MAINPID
PIDFile=/var/run/jobarchive.pid
KillMode=process

[Install]
WantedBy=multi-user.target
```
 
The program writes logging information to STDOUT and so is accessible via
/var/log/messages. By default the logging is silent, see the notes execution
below.

The program writes to the local job archival directory which is on the slurm controller host:
 - /var/slurm/jobscript_archive

Then we have a cron that runs every 5 minutes to move all data to a common user accessible path.

Our example cron:

    */5 * * * * slurm rsync -aArX --remove-source-files /var/slurm/jobscript_archive/ /common/jobscript_archive/
    
This recursively copies the contents of /var/slurm/job\_script archive, and creates a mirror out on /common/jobscript\_archive/. Note that it'll also copy the ACLs and removes the local copy on the scheduler host.

## Execution

The binary currently accepts two optional arguments.

    ./jobarchive [-d<X>] [-c <file>]

    Example: ./jobarchive -d1 -c groups.txt
    
The debug levels are:

 * 0 (default) - silent, except for errors
 * 1 - final do_processFiles status
 * 2 - final do_processFiles status plus re-try warnings
 * 3 - verbose

The -c indicates that you are providing a conf file. The conf file is a newline
delimited list of groups (one line separating each group) that'll be added to
the default ACLs of newly created top-level user directories, so all the child
files and directories will inherit that same ACL. We use this to give admins
access to view user job scripts and environment files.
