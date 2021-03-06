#!/usr/bin/env ipython

import numpy as np
import sys

from numpy import sqrt, sin, arcsin, cos, arccos, power, \
    log, abs, tan, arctan2, pi, zeros, array, arctan, dot, cross

# constants to transform the initial parameters to physical values
Re  = 6.37814e8
Me  = 5.97360e27
lem = 3.40000e41 # Canup (2001) value
#lem = 2.88993e41 # actual (?) value
G   = 6.67429e-8

rad2deg = 360/(2.*pi)

class GiantImpact(object):
  def __init__(self, mtar, mimp, Rtar, Rimp, G):
    nan = float('nan')
    self.mtar = mtar
    self.Rtar = Rtar
    
    self.mimp = mimp
    self.Rimp = Rimp

    self.G = G
    self.mtot = mtar + mimp
    self.mred = ( mtar * mimp ) / ( mtar + mimp )
    self.rimp = Rtar + Rimp
    self.Gmto = G*self.mtot
    self.vesc = sqrt( 2.*self.Gmto / self.rimp )

  def getInitVinfLimp(self, vinf, Limp, relsep):
    rimp = self.rimp
    mimp = self.mimp
    vesc = self.vesc
    
    vimp = sqrt(vinf*vinf + vesc*vesc)
    Lgraz = vimp*mimp*rimp
    impa = arcsin( Limp / Lgraz )

    return self.getInitVimpAlpha(vimp, impa, relsep)
  
  
  def getInitStateVecs(self, r0vect, v0vect):
    r0 = sqrt( dot( r0vect, r0vect) )
    v0 = sqrt( dot( v0vect, v0vect) )
    
    K      = self.Gmto
    k1     = r0 * v0 * v0 / K
    beta0  = arcsin( dot( r0vect, v0vect) / ( r0 * v0 ) )
    theta0 = arctan2( k1 * sin(beta0) * cos(beta0), \
        (k1 * cos(beta0) * cos(beta0) - 1.) )
      
    ev   = (cross( v0vect, cross( r0vect, v0vect) ) / K) - ( r0vect / r0 )
    e    = sqrt( dot( ev, ev ) )
    rp   = r0 * ((1. + e * cos(theta0)) / (1. + e))
    rimp = self.rimp
    mimp = self.mimp
    vimp = sqrt( v0*v0 - (2.*K/r0) + (2.*K/rimp) )

    Lgraz  = vimp*mimp*rimp
    h0vect  = cross( r0vect, v0vect )
    L0vect = h0vect*mimp
    L0     = sqrt( dot( L0vect, L0vect) )

    bscal = L0 / Lgraz
    impa = arcsin( bscal )
    self.impa = impa

    a = dot( h0vect, h0vect ) / ( K * ( 1. - e*e ) )
    T = 2.*pi*power( a, 3./2.) / sqrt(K)
    self.T = T
    
    relsep = r0 / rimp
    
    return self.getInitVimpAlpha(vimp, impa, relsep)

  def getInitVimpAlpha(self, vimp, impa, relsep):
    rimp = self.rimp
    mimp = self.mimp
    mtar = self.mtar
    vesc = self.vesc
    Gmto   = self.Gmto
    mtot = self.mtot

    k1    = rimp*(vimp*vimp)/Gmto
    bscal = sin(impa)
    Lgraz = vimp*mimp*rimp
    Limp  = bscal*Lgraz
    betaimp = (pi/2.)-impa

    self.L     = Limp
    self.Lgraz = Lgraz
    self.vesc  = vesc
    self.b     = Limp / Lgraz
    
    vinf = sqrt( vimp*vimp - vesc*vesc )
    self.vinf = vinf
    self.vimp  = vimp

    # reduced energy
    Ered = 0.5*vimp*vimp*(mimp*mtar)/(mimp + mtar)
    self.Ered = Ered
    
    e = sqrt((k1-1.)*(k1-1.)*cos(betaimp)*cos(betaimp) \
        + sin(betaimp)*sin(betaimp))
    thetaimp = arctan2(k1*sin(betaimp)*cos(betaimp), \
        k1*cos(betaimp)*cos(betaimp) - 1.)
    r0 = relsep*rimp
    v0 = sqrt( vinf*vinf + ( 2.*Gmto / r0 ) )
    
    # store as common
    self.e = e
    self.thetaimp = thetaimp

    logstr = "#  parameters:\n" +\
        "# vinf   = %12.6e  cm/s  " % vinf + "\n" +\
        "# vesc   = %12.6e  cm/s  " % vesc + "\n" +\
        "# Limp   = %12.6e gcm^2/s" % Limp + "\n" +\
        "# Lgraz  = %12.6e gcm^2/s" % Lgraz + "\n" +\
        "# gamma  = %12.6f        " % (mimp / mtar) + "\n" +\
        "# bscal  = %12.6f        " % bscal + "\n" +\
        "# e      = %12.6f        " % e     + "\n" +\
        "# Ered   = %12.3e  erg   " % Ered  + "\n" +\
        "#" + "\n"
    
    r0vect = array([0., 0., 0.])
    v0vect = array([0., 0., 0.])
    tinit = 0.

    # impact angle is >0., so we get a proper orbit
    if impa > 0.:
      rperih = rimp *( 1. + e*cos(thetaimp) ) / ( 1. + e )
      vperih = sqrt( vinf*vinf + ( 2.*Gmto / rperih ) )

      # determine true anomaly theta0 for r0
      theta0 = arccos( ( rperih*(1. + e ) - r0 ) / ( e * r0 ) )
      beta0  = arccos( Limp / ( r0*v0*mimp ) )
  
      if e > 1.001:
        a = rperih / ( e - 1. )
        k2 = sqrt( Gmto / (a*a*a) )
        timp = ( (e*sqrt(e*e-1.)*sin(thetaimp))/(1. + e*cos(thetaimp) ) \
            - log( (sqrt(e*e-1.) + (e - 1.)*tan( thetaimp / 2. ))/      \
            (sqrt(e*e-1.) - (e - 1.)*tan( thetaimp / 2. )) ) ) / k2
    
        t0 = ( (e*sqrt(e*e-1.)*sin(theta0))/(1. + e*cos(theta0) ) \
            - log( (sqrt(e*e-1.) + (e - 1.)*tan( theta0 / 2. ))/  \
            (sqrt(e*e-1.) - (e - 1.)*tan( theta0 / 2. )) ) ) / k2
        self.vartheta = arctan( 1. / sqrt( e*e - 1 ) )
        self.alphaneginf = -self.vartheta + thetaimp

      else:
        k2 = sqrt( Gmto / ( 8. * rperih*rperih*rperih) )
        timp = abs(0.5*(tan(thetaimp / 2.) + \
            (1./3.)*pow(tan(thetaimp / 2.),3.)) / k2)
        t0   = abs(0.5*(tan(theta0   / 2.) + \
            (1./3.)*pow(tan(theta0   / 2.),3.)) / k2)
        self.vartheta = thetaimp + pi
        self.alphaneginf = -self.vartheta + thetaimp

      r0vect[0] = -r0 * cos( pi/2. - thetaimp + theta0 )
      r0vect[1] =  r0 * sin( pi/2. - thetaimp + theta0 )
  
      v0vect[0] = -v0 * cos( theta0 - thetaimp - beta0 )
      v0vect[1] =  v0 * sin( theta0 - thetaimp - beta0 )

      # make sure the we have a positive Lz
      Lznorm = ( r0vect[0] * v0vect[1] - r0vect[0] * v0vect[1] )
      if ( Lznorm < 0. ):
      	r0vect[1] = -r0vect[1]
      	v0vect[1] = -v0vect[1]
      
      tinit = timp - t0
  
      logstr += \
          "#  perihelon:" + "\n" +\
          "# r      = %12.6e cm   " % rperih + "\n" +\
          "# v      = %12.6e cm/s " % vperih + "\n" +\
          "# theta  = %12.6f deg (true anomaly) " % 0. + "\n" +\
          "# t      = %12.6f s    " % ( timp ) + "\n" +\
          "#" + "\n" +\
          "#  impact:" + "\n" +\
          "# r      = %12.6e cm   " % rimp   + "\n" +\
          "# v      = %12.6e cm/s " % vimp   + "\n" +\
          "# theta  = %12.6f deg (true anomaly) " % (thetaimp*rad2deg) + "\n" +\
          "# beta   = %12.6f deg (impact angle) " % (betaimp*rad2deg) + "\n" +\
          "# t      = %12.6f s    " % 0. + "\n" +\
          "#" + "\n" +\
          "#  initial setup:" + "\n" +\
          "# r      = %12.6f rimp " % relsep + "\n" +\
          "# r      = %12.6e cm   " % r0 + "\n" +\
          "# v      = %12.6e cm/s " % v0 + "\n" +\
          "# theta  = %12.6f deg (true anomaly) " % (theta0*rad2deg) + "\n" +\
          "# beta   = %12.6f deg (impact angle) " % (beta0*rad2deg) + "\n" +\
          "# t      = %12.6f s    " % ( timp - t0 ) + "\n" +\
          "#" + "\n"
    
    
    else:
    # impact angle is 0., so we get a free fall
      r0vect[1] = r0
      v0vect[1] = -v0

      rscal = rimp / r0
      Gmto = self.Gmto
      tinit = -sqrt(r0*r0*r0/(2.*Gmto))*(sqrt(rscal*(1.-rscal)) + arccos(rscal))
      
      logstr += \
          "#  initial setup:" + "\n" +\
          "# r      = %12.6f rimp " % relsep + "\n" +\
          "# r      = %12.6e cm   " % r0 + "\n" +\
          "# v      = %12.6e cm/s " % v0 + "\n" +\
          "# t      = %12.6f s    " % tinit + "\n" +\
          "#" + "\n"
      self.alphainf = pi

    Ltotvect = cross( r0vect, v0vect )*self.mred
    Ltot     = sqrt( dot( Ltotvect, Ltotvect ) )
    self.Ltot = Ltot

    logstr += "# alpha  = %12.6f deg (incoming angle)" % \
               (self.alphaneginf*rad2deg) + "\n" + \
              "# Ltot   = %12.6e gcm^2/s" % Ltot + "\n#\n"
    
    
    r0tar = -( mimp / mtot )*r0vect
    r0imp =  ( mtar / mtot )*r0vect

    v0tar = -( mimp / mtot )*v0vect
    v0imp =  ( mtar / mtot )*v0vect
    
    
    logstr += \
        "# r0tar   = " + str(r0tar) + " cm" + "\n" +\
        "# r0imp   = " + str(r0imp) + " cm" + "\n" +\
        "# v0tar   = " + str(v0tar) + " cm/s" + "\n" +\
        "# v0imp   = " + str(v0imp) + " cm/s" + "\n" +\
        "#" + "\n"

    return (r0tar, r0imp, v0tar, v0imp, tinit, logstr)

  def getOrbit(self):
    pass


