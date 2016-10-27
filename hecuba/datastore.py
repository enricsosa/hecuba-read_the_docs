# author: G. Alomar
from hecuba.dict import *
from conf.apppath import apppath
import inspect
from pprint import pprint

def hecuba_filter(function, iterable):
    print "datastore hecuba_filter ####################################"
    inspectedfunction = inspect.getsource(function)
    if hasattr(iterable, 'indexed'):
        iterable.indexarguments = str(str(str(inspectedfunction).split(":")[1]).split(",")[0]).split(' and ')  # Args list
        print "type(iterable):             ", type(iterable)
        print "iterable.__class__.__name__:", iterable.__class__.__name__
        print "iterable.indexArguments:    ", iterable.indexArguments
        return iterable
    else:
        filtered = python_filter(function, iterable)
        return filtered

path = apppath + '/conf/storage_params.txt'

file = open(path, 'r')

for line in file:
    exec line

if not filter == hecuba_filter:
    python_filter = filter
    filter = hecuba_filter
