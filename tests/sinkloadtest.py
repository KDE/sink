#!/usr/bin/python3

import subprocess
import shlex
import os
import sys
import signal
import datetime

def execute(cmd):
    print (str(datetime.datetime.now()) + " Running command: ", cmd)
    popen = subprocess.Popen(shlex.split(cmd), universal_newlines=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    (stdoutdata, stderrdata) = popen.communicate()
    if popen.returncode:
        raise Exception("Something went wrong while running the command:", popen.returncode)
    return stdoutdata

def run(cmd, printOutput = False):
    execute(cmd)

def loadtest():
    resourceName = "kolabnowImap"
    run("sinksh create resource type sink.imap identifier {} server imaps://imap.kolabnow.com:993 username test1@kolab.org".format(resourceName))
    run("sinksh clear {}".format(resourceName))
    run("sinksh sync folder {} --password Welcome2KolabSystems".format(resourceName), printOutput = True)

    try:
        proc = subprocess.Popen(shlex.split("sinksh livequery mail --resource {}".format(resourceName)))

        run("sinksh sync mail {}/INBOX --password Welcome2KolabSystems".format(resourceName), printOutput = True)

    finally:
        proc.terminate()
        proc.communicate()
        #returncode -15 means signal 15 has terminated the process
        sig = -proc.returncode
        if sig != signal.SIGTERM:
            if sig == signal.SIGINT:
                raise KeyboardInterrupt()
            else:
                raise Exception("Something went wrong during the query: ", proc.returncode)

try:
    while True:
        loadtest();
except (KeyboardInterrupt, SystemExit):
    print("Aborted with Ctrl-c")
    sys.exit(0)
