from hecuba.iter import KeyIter
from hecuba.list import KeyList

class PersistentKeyList(KeyList):
    
    def __init__(self, mypdict):
        self.mypdict = mypdict
        

    def __iter__(self):
        return KeyIter(self)
        

    def getID(self):
	identifier = "%s_%s_kmeans_BlockData" % ( self.node, self.start_token )
	return identifier
