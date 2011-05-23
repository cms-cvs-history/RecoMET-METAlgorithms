#include "RecoMET/METAlgorithms/interface/SignAlgoResolutions.h"
// -*- C++ -*-
//
// Package:    METAlgorithms
// Class:      SignAlgoResolutions
// 
/**\class METSignificance SignAlgoResolutions.cc RecoMET/METAlgorithms/src/SignAlgoResolutions.cc

 Description: <one line class summary>

 Implementation:
     <Notes on implementation>
*/
//
// Original Author:  Kyle Story, Freya Blekman (Cornell University)
//         Created:  Fri Apr 18 11:58:33 CEST 2008
// $Id: SignAlgoResolutions.cc,v 1.7 2010/11/29 10:14:17 akhukhun Exp $
//
//
#include "FWCore/Framework/interface/EventSetup.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/ParticleFlowReco/interface/PFBlock.h"
#include "DataFormats/ParticleFlowReco/interface/PFBlockElement.h"


#include <math.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>

metsig::SignAlgoResolutions::SignAlgoResolutions(const edm::ParameterSet &iConfig):
    functionmap_(),
    ptResol_(0),
    phiResol_(0),
    pfcalib_(),
    pfresol_()
{
  addResolutions(iConfig);
}



double metsig::SignAlgoResolutions::eval(const resolutionType & type, const resolutionFunc & func, const double & et, const double & phi, const double & eta) const {
  // derive p from et and eta;
  double theta = 2*atan(exp(-eta));
  double p = et / sin(theta); // rough assumption: take e and p equivalent...
  return eval(type,func,et,phi,eta,p);

}

double metsig::SignAlgoResolutions::eval(const resolutionType & type, const resolutionFunc & func, const double & et, const double & phi, const double & eta, const double &p) const {

  functionPars x(4);
  x[0]=et;
  x[1]=phi;
  x[2]=eta;
  x[3]=p;
  //  std::cout << "getting function of type " << type << " " << func << " " << x[0] << " " << x[1] << " " << x[2] << " " << x[3] << std::endl;
  return getfunc(type,func,x);

}

metsig::SigInputObj  metsig::SignAlgoResolutions::evalPF(const reco::PFCandidate *candidate) const {
  double eta = candidate->eta();
  double phi = candidate->phi();
  double et = candidate->energy()*sin(candidate->theta());
  resolutionType thetype;
  std::string name;
  int type = candidate->particleId();
  switch (type) 
    {
    case 1: 
      thetype=PFtype1;name="PFChargedHadron"; break;
    case 2: 
      thetype=PFtype2;name="PFChargedEM"; break;
    case 3: 
      thetype=PFtype3;name="PFMuon"; break;
    case 4: 
      thetype=PFtype4;name="PFNeutralEM"; break;
    case 5: 
      thetype=PFtype5;name="PFNeutralHadron"; break;
    case 6: 
      thetype=PFtype6;name="PFtype6"; break;
    case 7:
      thetype=PFtype7;name="PFtype7"; break;
    default:
      thetype=PFtype7;name="PFunknown"; break;
  }

  double d_et=0, d_phi=0; //d_phi here is the error on phi component of the et
  reco::TrackRef trackRef = candidate->trackRef();
  if(!trackRef.isNull()){
    d_phi = et*trackRef->phiError();
    d_et  = (type==2) ?  ElectronPtResolution(candidate) : trackRef->ptError();
    //if(type==2) std::cout << eval(thetype,ET,et,phi,eta) << "    " << trackRef->ptError() << "    "<< ElectronPtResolution(candidate) << std::endl;
  }
  else{
    d_et = eval(thetype,ET,et,phi,eta);
    d_phi = eval(thetype,PHI,et,phi,eta);
  }

  metsig::SigInputObj resultingobj(name,et,phi,d_et,d_phi);
  return resultingobj;
}


metsig::SigInputObj
metsig::SignAlgoResolutions::evalPFJet(const reco::PFJet *jet) const{

    double jpt  = jet->pt();
    double jphi = jet->phi();
    double jeta = jet->eta();
    double jdeltapt = 999.;
    double jdeltapphi = 999.;

    if(jpt<ptResolThreshold_ && jpt<20.){ //use temporary fix for low pT jets
	double feta = TMath::Abs(jeta);
	int ieta = feta<5.? int(feta/0.5) : 9; //bin size = 0.5 
	int ipt  = jpt>3. ? int(jpt-3./2) : 0; //bin size =2, starting from ptmin=3GeV
	jdeltapt   = jdpt[ieta][ipt];
	jdeltapphi = jpt*jdphi[ieta][ipt];
    }
    else{
	TF1* fPtEta  = ptResol_->parameterEta("sigma",jeta);
	TF1* fPhiEta = phiResol_->parameterEta("sigma",jeta);
	jdeltapt   = jpt>ptResolThreshold_ ? jpt*fPtEta->Eval(jpt)  : jpt*fPtEta->Eval(ptResolThreshold_);
	jdeltapphi = jpt>ptResolThreshold_ ? jpt*fPhiEta->Eval(jpt) : jpt*fPhiEta->Eval(ptResolThreshold_);
	delete fPtEta;
	delete fPhiEta;
    }

    std::string inputtype = "jet";
    metsig::SigInputObj obj_jet(inputtype,jpt,jphi,jdeltapt,jdeltapphi);
    //std::cout << "RESOLUTIONS JET: " << jpt << "   " << jphi<< "   " <<jdeltapt << "   " << jdeltapphi << std::endl;
    return obj_jet;
}


void metsig::SignAlgoResolutions::addResolutions(const edm::ParameterSet &iConfig){
    using namespace std;

  // Jet Resolutions - for now load from the files. Migrate to EventSetup asap.
  metsig::SignAlgoResolutions::initializeJetResolutions( iConfig );
  
  ptResolThreshold_ = iConfig.getParameter<double>("ptresolthreshold");


    //get temporary low pT pfjet resolutions
    for (int ieta=0; ieta<10; ieta++){
      jdpt[ieta] = iConfig.getParameter<std::vector<double> >(Form("jdpt%d", ieta));
      jdphi[ieta] = iConfig.getParameter<std::vector<double> >(Form("jdphi%d", ieta));
    }


  // for now: do this by hand - this can obviously also be done via ESSource etc.
  functionPars etparameters(3,0);
  functionPars phiparameters(1,0);
  // set the parameters per function:
  // ECAL, BARREL:
  std::vector<double> ebet = iConfig.getParameter<std::vector<double> >("EB_EtResPar");
  std::vector<double> ebphi = iConfig.getParameter<std::vector<double> >("EB_PhiResPar");

  etparameters[0]=ebet[0];
  etparameters[1]=ebet[1];
  etparameters[2]=ebet[2];
  phiparameters[0]=ebphi[0];
  addfunction(caloEB,ET,etparameters);
  addfunction(caloEB,PHI,phiparameters);
 // ECAL, ENDCAP:
  std::vector<double> eeet = iConfig.getParameter<std::vector<double> >("EE_EtResPar");
  std::vector<double> eephi = iConfig.getParameter<std::vector<double> >("EE_PhiResPar");

  etparameters[0]=eeet[0];
  etparameters[1]=eeet[1];
  etparameters[2]=eeet[2];
  phiparameters[0]=eephi[0];
  addfunction(caloEE,ET,etparameters);
  addfunction(caloEE,PHI,phiparameters);
 // HCAL, BARREL:
  std::vector<double> hbet = iConfig.getParameter<std::vector<double> >("HB_EtResPar");
  std::vector<double> hbphi = iConfig.getParameter<std::vector<double> >("HB_PhiResPar");

  etparameters[0]=hbet[0];
  etparameters[1]=hbet[1];
  etparameters[2]=hbet[2];
  phiparameters[0]=hbphi[0];
  addfunction(caloHB,ET,etparameters);
  addfunction(caloHB,PHI,phiparameters);
 // HCAL, ENDCAP:
  std::vector<double> heet = iConfig.getParameter<std::vector<double> >("HE_EtResPar");
  std::vector<double> hephi = iConfig.getParameter<std::vector<double> >("HE_PhiResPar");

  etparameters[0]=heet[0];
  etparameters[1]=heet[1];
  etparameters[2]=heet[2];
  phiparameters[0]=hephi[0];
  addfunction(caloHE,ET,etparameters);
  addfunction(caloHE,PHI,phiparameters);
 // HCAL, Outer
  std::vector<double> hoet = iConfig.getParameter<std::vector<double> >("HO_EtResPar");
  std::vector<double> hophi = iConfig.getParameter<std::vector<double> >("HO_PhiResPar");


  etparameters[0]=hoet[0];
  etparameters[1]=hoet[1];
  etparameters[2]=hoet[2];
  phiparameters[0]=hophi[0];
  addfunction(caloHO,ET,etparameters);
  addfunction(caloHO,PHI,phiparameters);
 // HCAL, Forward
  std::vector<double> hfet = iConfig.getParameter<std::vector<double> >("HF_EtResPar");
  std::vector<double> hfphi = iConfig.getParameter<std::vector<double> >("HF_PhiResPar");

  etparameters[0]=hfet[0];
  etparameters[1]=hfet[1];
  etparameters[2]=hfet[2];
  phiparameters[0]=hfphi[0];
  addfunction(caloHF,ET,etparameters);
  addfunction(caloHF,PHI,phiparameters);


  // PF objects:
  // type 1:
  std::vector<double> pf1et = iConfig.getParameter<std::vector<double> >("PF_EtResType1");
  std::vector<double> pf1phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType1");
  etparameters[0]=pf1et[0];
  etparameters[1]=pf1et[1];
  etparameters[2]=pf1et[2];
  phiparameters[0]=pf1phi[0];
  addfunction(PFtype1,ET,etparameters);
  addfunction(PFtype1,PHI,phiparameters);

  // PF objects:
  // type 2:
  std::vector<double> pf2et = iConfig.getParameter<std::vector<double> >("PF_EtResType2");
  std::vector<double> pf2phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType2");
  etparameters[0]=pf2et[0];
  etparameters[1]=pf2et[1];
  etparameters[2]=pf2et[2];
  phiparameters[0]=pf2phi[0];
  addfunction(PFtype2,ET,etparameters);
  addfunction(PFtype2,PHI,phiparameters);

  // PF objects:
  // type 3:
  std::vector<double> pf3et = iConfig.getParameter<std::vector<double> >("PF_EtResType3");
  std::vector<double> pf3phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType3");
  etparameters[0]=pf3et[0];
  etparameters[1]=pf3et[1];
  etparameters[2]=pf3et[2];
  phiparameters[0]=pf3phi[0];
  addfunction(PFtype3,ET,etparameters);
  addfunction(PFtype3,PHI,phiparameters);

  // PF objects:
  // type 4:
  std::vector<double> pf4et = iConfig.getParameter<std::vector<double> >("PF_EtResType4");
  std::vector<double> pf4phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType4");
  etparameters[0]=pf4et[0];
  etparameters[1]=pf4et[1];
  etparameters[2]=pf4et[2];
  //phiparameters[0]=pf4phi[0];
  addfunction(PFtype4,ET,etparameters);
  addfunction(PFtype4,PHI,pf4phi); //use the same functional form for photon phi error as for pT, pass whole vector

  // PF objects:
  // type 5:
  std::vector<double> pf5et = iConfig.getParameter<std::vector<double> >("PF_EtResType5");
  std::vector<double> pf5phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType5");
  etparameters[0]=pf5et[0];
  etparameters[1]=pf5et[1];
  etparameters[2]=pf5et[2];
  phiparameters[0]=pf5phi[0];
  addfunction(PFtype5,ET,etparameters);
  addfunction(PFtype5,PHI,pf5phi);

  // PF objects:
  // type 6:
  std::vector<double> pf6et = iConfig.getParameter<std::vector<double> >("PF_EtResType6");
  std::vector<double> pf6phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType6");
  etparameters[0]=pf6et[0];
  etparameters[1]=pf6et[1];
  etparameters[2]=pf6et[2];
  phiparameters[0]=pf6phi[0];
  addfunction(PFtype6,ET,etparameters);
  addfunction(PFtype6,PHI,phiparameters);

  
  // PF objects:
  // type 7:
  std::vector<double> pf7et = iConfig.getParameter<std::vector<double> >("PF_EtResType7");
  std::vector<double> pf7phi = iConfig.getParameter<std::vector<double> >("PF_PhiResType7");
  etparameters[0]=pf7et[0];
  etparameters[1]=pf7et[1];
  etparameters[2]=pf7et[2];
  phiparameters[0]=pf7phi[0];
  addfunction(PFtype7,ET,etparameters);
  addfunction(PFtype7,PHI,phiparameters);

  return;
}

void metsig::SignAlgoResolutions::addfunction(resolutionType type, resolutionFunc func, functionPars parameters){

  functionCombo mypair(type,func);
  functionmap_[mypair]=parameters;

}

double metsig::SignAlgoResolutions::getfunc(const metsig::resolutionType & type,const metsig::resolutionFunc & func, functionPars & x) const{
  
  double result=0;
  functionCombo mypair(type,func);

  if(functionmap_.count(mypair)==0){
	return result;
  }
  
  functionPars values = (functionmap_.find(mypair))->second;
  switch ( func ){
  case metsig::ET :
    return EtFunction(x,values);
  case metsig::PHI :
    return PhiFunction(x,values);
  case metsig::TRACKP :
    return PFunction(x,values);
  case metsig::CONSTPHI :
    return PhiConstFunction(x,values);
  }
  
  //  std::cout << "returning function " << type << " " << func << " " << result << " " << x[0] << std::endl; 

  return result;
}

double metsig::SignAlgoResolutions::EtFunction( const functionPars &x, const functionPars & par) const
{
  if(par.size()<3)
    return 0.;
  if(x.size()<1)
    return 0.;
  double et=x[0];
  if(et<=0.)
    return 0.;
  double result = et*sqrt((par[2]*par[2])+(par[1]*par[1]/et)+(par[0]*par[0]/(et*et)));
  return result;
}


double metsig::SignAlgoResolutions::PhiFunction(const functionPars &x,const  functionPars & par) const
{
  double et=x[0];
  if(et<=0.){
    return 0.;
  }

  //if 1 parameter is C provided, returns C*pT, if three parameters N, S, C are provided, it returns the usual resolution value, as for sigmaPt
  if(par.size()!=1 && par.size()!=3){//only 1 or 3 parameters supported for phi function
      return 0.;
  }
  else if(par.size()==1){
    return par[0]*et;
  }
  else{
    return et*sqrt((par[2]*par[2])+(par[1]*par[1]/et)+(par[0]*par[0]/(et*et)));
  }

}
double metsig::SignAlgoResolutions::PFunction(const functionPars &x, const functionPars & par) const
{
  // not currently implemented
  return 0;
}

double metsig::SignAlgoResolutions::PhiConstFunction(const functionPars& x, const functionPars &par) const
{
  return par[0];
}

void
metsig::SignAlgoResolutions::initializeJetResolutions( const edm::ParameterSet &iConfig ) {
  
  using namespace std;
  
  // only reinitialize the resolutsion if the pointers are zero
  if ( ptResol_ == 0 ) {
    string resolutionsAlgo  = iConfig.getParameter<std::string>("resolutionsAlgo");     
    string resolutionsEra   = iConfig.getParameter<std::string>("resolutionsEra");     

    string cmssw_base(getenv("CMSSW_BASE"));
    string cmssw_release_base(getenv("CMSSW_RELEASE_BASE"));
    string path = cmssw_base + "/src/CondFormats/JetMETObjects/data";
    struct stat st;
    if (stat(path.c_str(),&st)!=0) {
      path = cmssw_release_base + "/src/CondFormats/JetMETObjects/data";
    }
    if (stat(path.c_str(),&st)!=0) {
      cerr<<"ERROR: tried to set path but failed, abort."<<endl;
    }    
    string era(resolutionsEra);
    string alg(resolutionsAlgo);
    string ptFileName  = path + "/" + era + "_PtResolution_" +alg+".txt";
    string phiFileName = path + "/" + era + "_PhiResolution_"+alg+".txt";
    
    ptResol_ = new JetResolution(ptFileName,false);
    phiResol_ = new JetResolution(phiFileName,false);
  }
}

double 
metsig::SignAlgoResolutions::ElectronPtResolution(const reco::PFCandidate *c) const{

    using namespace std;
    using namespace reco;

    bool applyCrackCorrections = false;

    //cout << " NEW ELECTRON:: momentum = " << c->p() <<  endl;

    reco::GsfTrackRef refGsf = c->gsfTrackRef();
      
    //double P_Track = refGsf->pMode();
    //double dP_Track = refGsf->ptModeError()*(refGsf->pMode()/refGsf->ptMode());
    //cout << " TRACK Momentum " <<  P_Track << " +/- " << dP_Track << " Eta " << refGsf->etaMode() << endl;

    // Re-compute the Electron Ecal energy and its error.
    double E_Ecal =0.; 
    double dE_Ecal=0.;
    const PFCandidate::ElementsInBlocks& theElements = c->elementsInBlocks();
    typedef PFCandidate::ElementsInBlocks::const_iterator IEB; 
    for (IEB ieb=theElements.begin(); ieb<theElements.end(); ++ieb) {
	const PFBlock& block = *(ieb->first);
	PFBlock::LinkData linkData =  block.linkData();
	const PFBlockElement& pfbe = block.elements()[ieb->second];
	
	if(pfbe.type()==reco::PFBlockElement::ECAL) {

	  // find the closest PS clusters
	  unsigned iEcal = ieb->second;
	  vector<double> ps1Ene;
	  vector<double> ps2Ene;
	  ps1Ene.clear();
	  ps2Ene.clear();
	
	  // Find PS1 clusters
	  std::multimap<double, unsigned int> PS1Elems;
	  block.associatedElements( iEcal,linkData,
				    PS1Elems,
				    reco::PFBlockElement::PS1,
				    reco::PFBlock::LINKTEST_ALL );
	  
	  for( std::multimap<double, unsigned int>::iterator it = PS1Elems.begin(); it != PS1Elems.end();it++) {
		unsigned int index = it->second;
		// Check that this cluster is not closer to another ECAL cluster
		std::multimap<double, unsigned> sortedECAL;
		block.associatedElements( index,  linkData,
				      sortedECAL,
				      reco::PFBlockElement::ECAL,
				      reco::PFBlock::LINKTEST_ALL );
		unsigned jEcal = sortedECAL.begin()->second;
		if ( jEcal != iEcal) continue; 
	    
		// push_back PS1 energy
		const PFBlockElement& psel = block.elements()[index];
		PFClusterRef  psref = psel.clusterRef();
		// cout << " PS1 Energy " << psref->energy() << endl;
		ps1Ene.push_back(psref->energy());
	  }

	  // Find PS2 clusters
	  std::multimap<double, unsigned int> PS2Elems;
	  block.associatedElements( iEcal,linkData,
				    PS2Elems,
				    reco::PFBlockElement::PS2,
				    reco::PFBlock::LINKTEST_ALL );
	  
	  for( std::multimap<double, unsigned int>::iterator it = PS2Elems.begin(); it != PS2Elems.end();it++) {
		unsigned int index = it->second;
		// Check that this cluster is not closer to another ECAL cluster
		std::multimap<double, unsigned> sortedECAL;
		block.associatedElements( index,  linkData,
				      sortedECAL,
				      reco::PFBlockElement::ECAL,
				      reco::PFBlock::LINKTEST_ALL );
		unsigned jEcal = sortedECAL.begin()->second;
		if ( jEcal != iEcal) continue; 
	    
		// push_back PS2 energy	    
		const PFBlockElement& psel = block.elements()[index];
		PFClusterRef  psref = psel.clusterRef();
		// cout << " PS2 Energy " << psref->energy() << endl;
		ps2Ene.push_back(psref->energy());
	  }
	  
	  
	  reco::PFClusterRef clusterRef = pfbe.clusterRef();
	  double ps1,ps2;
	  ps1=ps2=0.;
	  double E_Clust = pfcalib_->energyEm(*clusterRef,ps1Ene,ps2Ene,ps1,ps2,applyCrackCorrections);	  
	  double Eta_Clust = clusterRef->position().eta();
	  double dE_Clust = pfresol_->getEnergyResolutionEm(E_Clust,Eta_Clust);
	  
	  // cout << " ECAL Energy " << E_Clust << endl;
	  E_Ecal += E_Clust;
	  dE_Ecal += dE_Clust*dE_Clust;
	  
	}
    }
      
    // Ecal Energy and Error
    dE_Ecal = sqrt(dE_Ecal);
    //cout << " Raw ECAL Energy " << E_Ecal  << " +/- " << dE_Ecal << endl;

    // Take the corrected energy from the candidates and scale the error properly. 
    double E_EcalCorr = c->ecalEnergy();
    double dE_EcalCorr = dE_Ecal * (E_EcalCorr/E_Ecal); 

    //cout << " Corrected ECAL Energy " << E_EcalCorr  << " +/- " << dE_EcalCorr << endl;

    return dE_EcalCorr;
}

