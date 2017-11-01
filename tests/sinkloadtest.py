#!/usr/bin/python3

import subprocess
import shlex
import os
import signal
import datetime
# import json
# import time


def run(cmd):
    print ("Running command: ", cmd)
    result = subprocess.run(shlex.split(cmd), stdout=subprocess.PIPE)
    print("Returncode: ", result.returncode)
    if result.returncode != 0:
        print("Something went wrong while running the command:", result.returncode)
    # print(result.stdout.decode("utf-8"))

resourceName = "kolabnowImap"
run("sinksh create resource type sink.imap identifier {} server imaps://imap.kolabnow.com:993 username test1@kolab.org".format(resourceName))
run("sinksh clear {}".format(resourceName))
run("sinksh sync folder {} --password Welcome2KolabSystems".format(resourceName))

with subprocess.Popen(shlex.split("sinksh livequery mail --resource {}".format(resourceName)), stdout=subprocess.PIPE) as proc:
    run("sinksh sync mail {}/INBOX --password Welcome2KolabSystems".format(resourceName))
    # print("Sync is complete")
    proc.terminate()
    retcode = proc.wait()
    # print(proc.stdout.read().decode("utf-8"))
    if retcode != -15:
        print("Something went wrong during the query: ", retcode)
        print(str(datetime.now()))

# run("sinksh sync mail {}/INBOX --password Welcome2KolabSystems".format(resourceName))
# proc.abort()
# run("sinksh count mail {}".format(resourceName))
# run("sinksh list mail --resource {}".format(resourceName))
# proc.wait()


'''
sinksh sync
sinksh count
sinksh list
sinksh livequery

sinksh show

sinksh create
sinksh modify
sinksh delete

'''


# sinksh sync imapresource1

# sinksh sync mail {5718e105-57f6-424d-9c11-61b8e9b64d11}/INBOX --password mykolab@PlsiaCT\!01
# output = subprocess.check_output(hawdCommand + " json " + benchmark["hawd_def"] + " || true", shell=True, stderr=subprocess.STDOUT)
# result = subprocess.run("sinksh livequery mail --resource {5718e105-57f6-424d-9c11-61b8e9b64d11}", check=True, stdout=subprocess.PIPE).stdout
# result = subprocess.run(["sinksh", "list", "mail", "--resource {5718e105-57f6-424d-9c11-61b8e9b64d11}"], check=True, stdout=subprocess.PIPE).stdout
# result = subprocess.run(["sinksh", "list", "mail", "--resource {5718e105-57f6-424d-9c11-61b8e9b64d11}"], check=True, stdout=subprocess.PIPE)
# print("Resource ")
# print(result.stdout.decode("utf-8"))

