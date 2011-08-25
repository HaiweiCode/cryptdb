#!/usr/bin/python


# To run tput test
# install phpBB on CryptDB and run the trace generated by createstate.py through the proxy
# login as admin and give permissions to newly registered users to read and write posts 
# (for example, a simple way, go to admin panel, select groups, newly registered users, all features) 
# send at least one private message to each user, say have the admin do this by sending to more at once
# set limit of priv msgs to 1000

import sys
import os
import time
import signal
import run_tests
import random

baseusername = "user"
basecookie = "cookie"
basehtml = "htmlfile"
filename = "stats"


noreadm = 1
noreadp = 1
noreadm = 1
nowritem = 1
stat = run_tests.stats()
#      no_read_m noreadp  nowritem nowritep  norepeats
task = ["5",     "5",     "1",     "1",      "1"]  
interval = -1

def getuserfile(basefile, u, i):
	return basefile + repr(u) + "_" + repr(i)

def getusername(u):
	return baseusername + repr(u)

def workerFinish():
	assert stat.worker != 0, "not a worker"
	f = open(filename, 'a')
	f.write(str(stat.worker) + " " + str(stat.queries_failed) + " " + str(stat.total_queries) + " " + str(stat.spassed) + "\n")
	f.close()
	#print "->worker", stat.worker, "queriesfailed", stat.queries_failed, "queriestotal", stat.total_queries, "spassed", stat.spassed
	exit(os.EX_OK)

def handler(signum, frame):
	secs = random.random() % 1000
	time.sleep(secs)
	workerFinish();

def worker(u, i):
	username =  getusername(u)
	# if even, do write first, else do reads first
	query = ["run_tests.py", username]
	query.extend(task)
	query.extend([getuserfile(basehtml, u, i),getuserfile(basecookie, u, i)])
	if u % 2 == 0:
		query.append("1") # do reads first
	else:
		query.append("0") # do writes first
	assert len(query) == 10, "wrong number of arguments"
	stat.set(u)
	assert stat.worker != 0, "not a worker :-("
	res = run_tests.run(query,stat)

	workerFinish()

def main(arg):
	global interval
	if len(arg) != 5:
		print("wrong number of arguments: runtput.py useridstart nousers noinstancesperuser no-preliminary-posts")
		return

	os.system("rm cookie* html* "+filename)
	os.system("touch "+filename)
	useridstart = int(arg[1])
	users = int(arg[2])
	print 'users ' +  repr(users)
	instances = int(arg[3])
	print 'instances ' + repr(instances)
	posts = int(arg[4])

	create = "python createstate.py " + str(users) + " " + str(users) + " " + str(posts) + " h o"
	os.system(create)
	
	try:
		signal.signal(signal.SIGTERM, handler)
	except ValueError:
		assert False, "setting signal failed"

	pids = {}
	index = -1
	start = time.time()
	
	for u in range(0, users):
		for i in range(0, instances):
			index = index + 1
			pid = os.fork()
			if pid>0:
				#in parent now
				pids[index] = pid
			elif pid<0:
				#error
				print "failed to fork"
				return
			else:  
				#out of parent
				worker(u+useridstart,i)
				
	index = -1

	(firstfinished, exit_status) = os.waitpid(-1, 0)
	assert exit_status == os.EX_OK, "first child exited irregularly"
	
	for u in range(0, users):
		for i in range(0, instances):
			index = index + 1
			if pids[index] != firstfinished:
				os.kill(pids[index], signal.SIGTERM)
	
	index = -1
	for u in range(0, users):
		for i in range(0, instances):
			index = index + 1
			if pids[index] != firstfinished:
				if (os.waitpid(pids[index],0)[1] != os.EX_OK):
					print "there were problems with process", index

	f = open(filename,'r')
	total_queries = 0
	good_queries = 0
	querytput = 0.0
	querylat = 0.0
	#first line has the firstfinished child, so use that interval
	first = True
	for line in f:
		words = line.split()
		assert len(words) == 4, "wrong number of words in line"
		if line == "":
			continue
		if first:
			interval = float(words[3])			
			first = False
		total_queries += int(words[2])
		good_queries += (int(words[2]) - int(words[1]))
		#print "worker", words[0], "queriesfailed", words[1], "queriestotal", words[2], "mspassed", words[3]

	querytput = good_queries/interval
	querylat = (interval*users*instances)/total_queries

	os.system("rm html* cookie*")

	end = time.time()
	print 'interval of time is ' + repr(end-start)
	print "throughput is", querytput
	print "latency is", querylat
 
main(sys.argv)  
