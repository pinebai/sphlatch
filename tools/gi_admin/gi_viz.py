import os
import os.path as path
import stat
import shelve

from gi_plot import GIplotConfig, GIplot

class GIvizTask(object):
  def __init__(self, pfile, cfile, ifile, plotcfg, ax, tscl, params):
    self.pfile = pfile
    self.cfile = cfile
    self.ifile = ifile

    self.plotcfg = plotcfg
    self.ax      = ax
    self.tscl    = tscl
    self.params  = params

  def execute(self):
    gipl = GIplot(self.pfile, self.cfile)
    gipl.plotParticles()
    gipl.plotDumpTime()
    #gipl.plotSimParams(self.params)
    gipl.plotClumps(self.tscl)
    gipl.storePlot(self.ifile, self.ax)


class GIviz(object):
  def __init__(self, cfgfile, plotcfg):
    self.cfg = {}
    if path.exists(cfgfile):
      execfile(cfgfile, self.cfg)
      print cfgfile + " loaded"
    
    self.dir = self.cfg["VIZDIR"] + "/"
    if not path.exists(self.dir):
      os.mkdir(self.dir)

    self.scdir = self.cfg["VIZSCRATCHDIR"] + "/"
    if not path.exists(self.scdir):
      os.mkdir(self.scdir)

    self.plotcfg = plotcfg
    self.tasksperjob = self.cfg["TASKSPERJOB"]

  def vizSim(self, sim, ax=[0., 0., 1., 1.]):
    scdir = self.scdir
    simkey = sim.params.key
    
    tasks = {}
    self.addPlotTasks(tasks, sim, ax)
    self.plotJobScript(tasks, simkey)


  def addPlotTasks(self, tasks, sim, ax=[0., 0., 1., 1.]):
    dumps = sim.dumps
    for dumprec in dumps:
      (dfile, dtime) = dumprec
      dprfx = dfile.replace('.h5part','')
      pfile = sim.dir + dfile
      cfile = sim.dir + "clumps/" + dfile

      key = sim.params.key + "_" + dprfx

      idir  = self.dir + sim.params.key + "/"
      ifile = idir + dprfx + self.plotcfg.imgext

      if not path.exists(idir):
        os.mkdir(idir)

      if path.exists(pfile) and path.exists(cfile) and not path.exists(ifile):
        open(ifile,'w').close()
        tasks[key] = GIvizTask(pfile, cfile, ifile, \
            self.plotcfg, ax, sim.tscl, sim.params)


  def plotJobScript(self, tasks, jobname):
    ascdir = os.path.abspath( self.scdir ) + "/"
    excname = ascdir + jobname + "_exec.py"
    sdbname = ascdir + jobname
    
    excstr = "#!/usr/bin/env python\n" +\
        "from gi_viz import GIvizTask\n" +\
        "from gi_plot import GIplot\n" +\
        "import shelve, sys\n" +\
        "taskkey = sys.argv[1]\n" +\
        "tasks = shelve.open(\"" + sdbname + "\")\n" +\
        "tasks[taskkey].execute()\n" +\
        "tasks.close()\n"

    excfile = open(excname,"w")
    print >>excfile, excstr
    excfile.close()
    os.chmod(excname, stat.S_IRWXU)

    drvno   = 0
    taskno  = 0
    drvname = ascdir + jobname + "_driver" + ("%04d" % drvno ) + ".sh"
    drvhead = "#!/bin/bash \n"
    drvfoot = "rm " + drvname + "\n"
    drvstr  = drvhead

    taskdb = shelve.open(sdbname)
    taskdb.clear()
    for tkey in tasks.keys():
      taskdb[tkey] = tasks[tkey]

      drvstr += "python " + excname + " " + tkey + "\n"
      taskno += 1
      if taskno > self.tasksperjob:
        taskno = 0
        drvstr += drvfoot
        drvfile = open(drvname,"w")
        print >>drvfile, drvstr
        print drvstr
        drvfile.close()
        
        os.chmod(drvname, stat.S_IRWXU)
        
        drvstr = drvhead
        drvno += 1
        drvname = ascdir + jobname + "driver" + ("%04d" % drvno ) + ".sh"
    taskdb.close()
        
    drvstr += drvfoot
    drvfile = open(drvname,"w")
    print >>drvfile, drvstr
    print drvstr
    drvfile.close()
    os.chmod(drvname, stat.S_IRWXU)


    #drvstr += "rm " + sdbname + "\n"
    #drvstr += "rm " + excname + "\n"
    drvstr += "rm " + drvname + "\n"



