# Very rudimentary test of thread module

# Create a bunch of threads, let each do some work, wait until all are done

from test_support import verbose
import random
import thread
import time

mutex = thread.allocate_lock()
rmutex = thread.allocate_lock() # for calls to random
running = 0
done = thread.allocate_lock()
done.acquire()

numtasks = 10

def task(ident):
	global running
	rmutex.acquire()
	delay = random.random() * numtasks
	rmutex.release()
	if verbose:
	    print 'task', ident, 'will run for', round(delay, 1), 'sec'
	time.sleep(delay)
	if verbose:
	    print 'task', ident, 'done'
	mutex.acquire()
	running = running - 1
	if running == 0:
		done.release()
	mutex.release()

next_ident = 0
def newtask():
	global next_ident, running
	mutex.acquire()
	next_ident = next_ident + 1
	if verbose:
	    print 'creating task', next_ident
	thread.start_new_thread(task, (next_ident,))
	running = running + 1
	mutex.release()

for i in range(numtasks):
	newtask()

print 'waiting for all tasks to complete'
done.acquire()
print 'all tasks done'

class barrier:
	def __init__(self, n):
		self.n = n
		self.waiting = 0
		self.checkin  = thread.allocate_lock()
		self.checkout = thread.allocate_lock()
		self.checkout.acquire()

	def enter(self):
		checkin, checkout = self.checkin, self.checkout

		checkin.acquire()
		self.waiting = self.waiting + 1
		if self.waiting == self.n:
			self.waiting = self.n - 1
			checkout.release()
			return
		checkin.release()

		checkout.acquire()
		self.waiting = self.waiting - 1
		if self.waiting == 0:
			checkin.release()
			return
		checkout.release()

numtrips = 3
def task2(ident):
	global running
	for i in range(numtrips):
		if ident == 0:
			# give it a good chance to enter the next
			# barrier before the others are all out
			# of the current one
			delay = 0.001
		else:
			rmutex.acquire()
			delay = random.random() * numtasks
			rmutex.release()
		if verbose:
		    print 'task', ident, 'will run for', round(delay, 1), 'sec'
		time.sleep(delay)
		if verbose:
		    print 'task', ident, 'entering barrier', i
		bar.enter()
		if verbose:
		    print 'task', ident, 'leaving barrier', i
	mutex.acquire()
	running = running - 1
	if running == 0:
		done.release()
	mutex.release()

print '\n*** Barrier Test ***'
if done.acquire(0):
	raise ValueError, "'done' should have remained acquired"
bar = barrier(numtasks)
running = numtasks
for i in range(numtasks):
	thread.start_new_thread(task2, (i,))
done.acquire()
print 'all tasks done'
