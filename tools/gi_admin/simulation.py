import os
import os.path as path
import time
import commands
import shelve
import numpy as np
import shutil

from numpy import sqrt, sin, cos, arccos, log, abs, tan, arctan2, pi, zeros
from body import BodyFile
from setup_gi import GiantImpact
import pdb



rad2deg = 360./(2.*pi)
deg2rad = 1./rad2deg

# constants to transform the initial parameters to physical values
G   = 6.67429e-8
Re  = 6.37814e8
Me  = 5.97360e27
lem = 3.40000e41 # Canup (2001) value
#lem = 2.88993e41 # actual (?) value

def resolvePath(_path):
  return path.abspath( path.expanduser(_path) )

class Logger(object):
  logfile = []

  def __init__(self, filename):
    self.logfile = open(filename, 'a')

  def __del__(self):
    self.logfile.close()

  def write(self, str):
    if len(str) > 0:
      self.logfile.write(time.ctime() + ":   " + str + "\n")
      self.logfile.flush()

class SimParams(object):
  def __init__(self, mimp, mtar, impa, vimprel, cfg):
    self.mimp = mimp
    self.mtar = mtar
    self.impa = impa
    self.vimprel = vimprel

    self.cfg = cfg

    self.temp = float(cfg["TEMP"])

    self.key = 'mtar%07.3f' % mtar + '_' \
      + 'mimp%07.3f' % mimp + '_' \
      + 'impa%04.1f' % impa + '_' \
      + 'vimp%04.1f' % vimprel


class Simulation(object):
  # noCPUs, 
  def __init__(self,params):
    self.params = params
    ssbdir = resolvePath(params.cfg["SIMSETBDIR"]) + "/"
    self.dir = resolvePath(ssbdir + "sim_" + params.key) + "/"
    
    if path.exists(self.dir):
      self.Log = Logger(self.dir + "setup.log")
    else:
      self.Log = Logger("/dev/null")

    self.jobid = 0

    self.state = "unknown"
    self.getState()
    self._setOrbit()

  def setError(self, str=None):
    if not str == None and hasattr(self, "Log"):
      self.Log.write("error: " + str)
    self.state = "error"
    self.next = self._doNothing
    return self.state

  def getState(self):
    if self.state == "error":
      return self.state

    if not path.exists(self.dir):
      self.state = "unprepared"
      self.next = self._prepare
      return self.state
    else:
      neededfiles = self.params.cfg["AUXFILES"].split()
      neededfiles.append(self.params.cfg["BINARY"])
      neededfiles.append("initial.h5part")

      for file in neededfiles:
        if not os.path.exists(self.dir + os.path.split(file)[1]):
          self.state = "unprepared"
          self.next = self._prepare
          return self.state
      # so everything's ready
      if self.jobid == 0:
        self.state = "prepared" 
        self.next = self._submit
      return self.state

    self.state = "unknown"
    self.next = self._doNothing
    return self.state

  def reset(self):
    self.state = "unprepared"
    self.next = self._prepare
    return self.state

  def _doNothing(self):
    return self.state
  
  def _setOrbit(self):
    # find fitting bodies
    (self.tarb, self.impb) = self._findBodies()

    mtar = self.tarb.m
    mimp = self.impb.m
    Rtar = self.tarb.r
    Rimp = self.impb.r
    
    G = float(self.params.cfg["GRAVCONST"])
    relsep = float(self.params.cfg["RELSEP"])
    
    # get giant impact
    gi = GiantImpact(mtar, mimp, Rtar, Rimp, G)
    vimp = self.params.vimprel * gi.vesc
    impa = self.params.impa * deg2rad

    (self.r0tar, self.r0imp, self.v0tar, self.v0imp, t0, self.gilogstr) = \
        gi.getInitVimpAlpha(vimp, impa, relsep)

    tscal = (Rtar + Rimp)/gi.vesc
    self.tscl = tscal
    self.vimp = vimp
    self.G    = G
    
    self.tstop = tscal * float(self.params.cfg["TSCALKSTOP"])
    self.tdump = tscal * float(self.params.cfg["TSCALKDUMP"])
    self.tanim = tscal * float(self.params.cfg["TSCALKANIM"])
    self.t0 = t0 - ( t0 % self.tdump )


  def _findDumps(self):
    dumps = []
    for file in os.listdir(self.dir):
      if file[0:4] == "dump" and file[-6:] == "h5part":
        cmd = "h5part_readattr -k time -i " + self.dir + file
        print cmd
        (stat, out) = commands.getstatusoutput(cmd)
        time = float("nan")
        if stat == 0:
          time = float(out.split()[1])
        dumps.append((self.dir + file, time))
    return dumps

  def _prepare(self):
    # if it does not yet exist, make dir
    if not path.exists(self.dir):
      os.mkdir(self.dir)
      self.Log = Logger(self.dir + "setup.log")

    # copy files
    auxf = self.params.cfg["AUXFILES"].split()
    
    for file in auxf:
      shutil.copy2(resolvePath(file), self.dir)
    
    self.Log.write("auxiliary files copied")
    
    if path.exists(self.dir + "initial.h5part"):
      os.remove(self.dir + "initial.h5part")

    # get orbit
    gilog = open(self.dir + "gi_setup.log", "w")
    print >>gilog, self.gilogstr
    gilog.close()
    self.Log.write("giant impact calculated")

    # displace bodies
    r0tar = self.r0tar
    r0imp = self.r0imp
    v0tar = self.v0tar
    v0imp = self.v0imp

    cmds = []
    cmds.append("cp -Rpv " + self.tarb.file + " " + self.dir + "tarb.h5part")
    cmds.append("h5part_displace -i " + self.dir + "tarb.h5part " +\
        "--pos [%e,%e,%e] " % (r0tar[0], r0tar[1], r0tar[2]) +\
        "--vel [%e,%e,%e] " % (v0tar[0], v0tar[1], v0tar[2]) )
    cmds.append("cp -Rpv " + self.impb.file + " " + self.dir + "impb.h5part")
    cmds.append("h5part_displace -i " + self.dir + "impb.h5part " +\
        "--pos [%e,%e,%e] " % (r0imp[0], r0imp[1], r0imp[2]) +\
        "--vel [%e,%e,%e] " % (v0imp[0], v0imp[1], v0imp[2]) +\
        "--id 2000000" )
    cmds.append("h5part_combine -a " + self.dir + "tarb.h5part -b " +\
        self.dir + "impb.h5part -o " + self.dir + "initial.h5part")
    cmds.append("rm " + self.dir + "tarb.h5part " + self.dir + "impb.h5part")
    
    # write attributes (gravconst, attrs)
    attrkeys = self.params.cfg["ATTRKEYS"].split()
    attrvals = self.params.cfg["ATTRVALS"].split()

    attrkeys.extend(["time", "gravconst", "tscal"])
    attrvals.extend([str(self.t0), str(self.G), str(self.tscl)])

    for key,val in zip(attrkeys, attrvals):
      cmds.append("h5part_writeattr -i " + self.dir + "initial.h5part " +\
          " -k " + key + " -v " + val)

    # prepare and copy binary
    if not self.params.cfg.has_key("BINFILE"):
      srcdir = resolvePath(self.params.cfg["SRCDIR"]) + "/"
      targ = self.params.cfg["MAKETARG"]
      oldwd = os.getcwd()
      os.chdir(srcdir)
      (stat, out) = commands.getstatusoutput("make " + targ)
      os.chdir(oldwd)
      self.Log.write("binary compiled")
      self.params.cfg["BINFILE"] = \
          resolvePath(srcdir + self.params.cfg["BINARY"])
    binf = self.params.cfg["BINFILE"]
    cmds.append("cp -Rpv " + binf + " " + self.dir)

    # now execute the commands
    for cmd in cmds:
      (stat, out) = commands.getstatusoutput(cmd)
      self.Log.write(out)
      if not stat == 0:
        self.setError(cmd + " failed")
        return self.state
    
    self.next = self._submit
    self.Log.write("prepared state")
    self.state = "prepared"
    return self.state

  def _submit(self):
    if not self.getState() == "prepared":
      self.setError("tried to submit from a non-prepared state")
      return self.state
    
    subcmd = self.params.cfg["SUBCMD"]
    nocpus = self.params.cfg["NOCPUS"]
    binary = self.params.cfg["BINARY"]
    runarg = self.params.cfg["RUNARGS"]
    sstnam = self.params.cfg["SIMSETNAME"]

    self.jobname = sstnam + "_" + self.params.key
  
    nodumps = float( self.params.cfg["NODUMPS"] )
    k       = float( self.params.cfg["TSCALK"] )

    stoptime = self.tscal * k
    savetime = round( stoptime / (60.*nodumps) ) * 60.

    self.Log.write("tscal = %9.3e" % self.tscal)
    self.Log.write("tstop = %9.3e" % stoptime)

    subcmd = subcmd.replace('$NOCPUS' , str(nocpus))
    subcmd = subcmd.replace('$SIMNAME', self.jobname)
    subcmd = subcmd.replace('$BINARY' , binary)
    
    subcmd = subcmd.replace('$SAVETIME' , str(savetime))
    subcmd = subcmd.replace('$STOPTIME' , str(stoptime))
    subcmd = subcmd.replace('$RUNARGS' ,  runarg)

    oldwd = os.getcwd()
    os.chdir(self.dir)
    (stat, out) = commands.getstatusoutput(subcmd)
    os.chdir(oldwd)

    if not stat == 0:
      self.setError("submit command failed (" + out + ")")
    else:
      self.next = self._doNothing
      self.state = "submitted"
      self.Log.write("job \"" + self.jobname + "\" submitted")
    return self.state

  def setSGEjobid(self, jobid):
    self.Log.write("SGE set job id to " + str(jobid))
    self.jobid = jobid

  def setSGEstat(self, str):
    if self.state == "error":
      return
      
    if str == "queued":
      self.next = self._doNothing
      self.state = str

    if str == "run":
      self.next = self._postproc
      self.state = str

    if str == "finished":
      self.next = self._finalize
      self.state = str

  def _postproc(self):
    if self.state == "run":

      pass

    elif self.state == "finished":
      self.Log.write("SGE reported finished state, was job " + str(self.id))
      self.next = self._doNothing
    else:
      self.setError("tried to postproc from a non-submitted state")
    return self.state

  def _finalize(self):
    print self.state
    if not self.state == "finished":
      self.setError("tried to finalize from non-finished state")
    else:
      self.next = self._doNothing
      self.state = "finalized"
      self.Log.write("finalized simulation")

    return self.state

  def _findBodies(self):
    nan = float('nan')

    toler = float(self.params.cfg["BODTOLERANCE"])
    boddb = shelve.open(resolvePath(self.params.cfg["BODIESDB"]))

    # find target candidates
    targtmpl = BodyFile("", Me*self.params.mtar, nan, \
        self.params.temp, nan, {})
    targcand = []
    for key in boddb.keys():
      if boddb[key].isSimilar(targtmpl, toler):
        targcand.append(boddb[key])

    # find impactor candidates
    impatmpl = BodyFile("", Me*self.params.mimp, nan, \
        self.params.temp, nan, {})
    impacand = []
    for key in boddb.keys():
      if boddb[key].isSimilar(impatmpl, toler):
        impacand.append(boddb[key])
    
    boddb.close()
    
    # find fitting pairs
    pairs = []
    for tarbd in targcand:
      for impbd in impacand:
        if abs((impbd.h - tarbd.h)/tarbd.h) < toler:
          pairs.append( (tarbd, impbd) )
    
    self.Log.write("found " + str(len(pairs)) + " matching body pairs")

    if len(pairs) == 0:
      pairs.append( (None, None) )
    else:
      # sort according to smoothing length
      pairs.sort(key=lambda bod: bod[0].h)
    
    return pairs[0]


class SimParams(object):
  def __init__(self, mimp, mtar, impa, vimprel, cfg):
    self.mimp = mimp
    self.mtar = mtar
    self.impa = impa
    self.vimprel = vimprel

    self.cfg = cfg

    self.temp = float(cfg["TEMP"])

    self.key = 'mtar%07.3f' % mtar + '_' \
      + 'mimp%07.3f' % mimp + '_' \
      + 'impa%04.1f' % impa + '_' \
      + 'vimp%04.1f' % vimprel


class SimSet(object):
  sims = {}
  def __init__(self, cfgfile):
    self.simsetcfg = {}
    
    if path.exists(cfgfile):
      execfile(cfgfile, self.simsetcfg)
      print cfgfile + " loaded"
    else:
      print cfgfile + " not found!   exiting ..."
    
    bdir = self.simsetcfg["SIMSETBDIR"]
    if not path.exists(bdir):
      os.mkdir(bdir)

    logfile = self.simsetcfg["LOGFILE"]
    self.Log = Logger(logfile)
    self.Log.write("new SimSet")
    
    mtara = np.array( self.simsetcfg["MTAR"].split(), dtype=float)
    mimpa = np.array( self.simsetcfg["MIMP"].split(), dtype=float)
    vimprela = np.array( self.simsetcfg["VIMPREL"].split(), dtype=float)
    impaa = np.array( self.simsetcfg["IMPA"].split(), dtype=float)

    cond = self.simsetcfg["SIMCOND"]
    if len(cond) < 1:
      cond = "True"

    for mtar in mtara:
      for mimp in mimpa:
        for vimprel in vimprela:
          for impa in impaa:
            if ( eval(cond) ):
              simpar = SimParams(mimp, mtar, impa, vimprel, self.simsetcfg)
              if not self.sims.has_key(simpar.key):
                self.sims[simpar.key] = Simulation( simpar )


  def __del__(self):
    pass
