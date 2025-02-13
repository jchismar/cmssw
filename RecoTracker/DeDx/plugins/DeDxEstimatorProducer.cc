// -*- C++ -*-
//
// Package:    DeDxEstimatorProducer
// Class:      DeDxEstimatorProducer
//
/**\class DeDxEstimatorProducer DeDxEstimatorProducer.cc RecoTracker/DeDxEstimatorProducer/src/DeDxEstimatorProducer.cc

 Description: <one line class summary>

 Implementation:
     <Notes on implementation>
*/
//
// Original Author:  andrea
//         Created:  Thu May 31 14:09:02 CEST 2007
//    Code Updates:  loic Quertenmont (querten)
//         Created:  Thu May 10 14:09:02 CEST 2008
//
//

// system include files
#include "RecoTracker/DeDx/plugins/DeDxEstimatorProducer.h"
#include "FWCore/Framework/interface/ConsumesCollector.h"

using namespace reco;
using namespace std;
using namespace edm;

void DeDxEstimatorProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<string>("estimator", "generic");
  desc.add<edm::InputTag>("tracks", edm::InputTag("generalTracks"));
  desc.add<bool>("UseDeDxHits", false);
  desc.add<edm::InputTag>("pixelDeDxHits", {});
  desc.add<edm::InputTag>("stripDeDxHits", {});
  desc.add<bool>("UsePixel", false);
  desc.add<bool>("UseStrip", true);
  desc.add<double>("MeVperADCPixel", 3.61e-06);
  desc.add<double>("MeVperADCStrip", 3.61e-06 * 265);
  desc.add<bool>("ShapeTest", true);
  desc.add<bool>("UseCalibration", false);
  desc.add<string>("calibrationPath", "");
  desc.add<string>("Record", "SiStripDeDxMip_3D_Rcd");
  desc.add<string>("ProbabilityMode", "Accumulation");
  desc.add<double>("fraction", 0.4);
  desc.add<double>("exponent", -2.0);
  desc.add<bool>("truncate", true);

  descriptions.add("DeDxEstimatorProducer", desc);
}

DeDxEstimatorProducer::DeDxEstimatorProducer(const edm::ParameterSet& iConfig)
    : tkGeomToken(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord, edm::Transition::BeginRun>()) {
  produces<ValueMap<DeDxData>>();

  auto cCollector = consumesCollector();
  string estimatorName = iConfig.getParameter<string>("estimator");
  if (estimatorName == "median")
    m_estimator = std::make_unique<MedianDeDxEstimator>(iConfig);
  else if (estimatorName == "generic")
    m_estimator = std::make_unique<GenericAverageDeDxEstimator>(iConfig);
  else if (estimatorName == "truncated")
    m_estimator = std::make_unique<TruncatedAverageDeDxEstimator>(iConfig);
  else if (estimatorName == "genericTruncated")
    m_estimator = std::make_unique<GenericTruncatedAverageDeDxEstimator>(iConfig);
  else if (estimatorName == "unbinnedFit")
    m_estimator = std::make_unique<UnbinnedFitDeDxEstimator>(iConfig);
  else if (estimatorName == "likelihoodFit")
    m_estimator = std::make_unique<LikelihoodFitDeDxEstimator>(iConfig);
  else if (estimatorName == "productDiscrim")
    m_estimator = std::make_unique<ProductDeDxDiscriminator>(iConfig, cCollector);
  else if (estimatorName == "btagDiscrim")
    m_estimator = std::make_unique<BTagLikeDeDxDiscriminator>(iConfig, cCollector);
  else if (estimatorName == "smirnovDiscrim")
    m_estimator = std::make_unique<SmirnovDeDxDiscriminator>(iConfig, cCollector);
  else if (estimatorName == "asmirnovDiscrim")
    m_estimator = std::make_unique<ASmirnovDeDxDiscriminator>(iConfig, cCollector);

  //Commented for now, might be used in the future
  //   MaxNrStrips         = iConfig.getUntrackedParameter<unsigned>("maxNrStrips"        ,  255);

  m_tracksTag = consumes<reco::TrackCollection>(iConfig.getParameter<edm::InputTag>("tracks"));

  useDeDxHits = iConfig.getParameter<bool>("UseDeDxHits");
  m_pixelDeDxHitsTag = consumes<reco::TrackDeDxHitsCollection>(iConfig.getParameter<edm::InputTag>("pixelDeDxHits"));
  m_stripDeDxHitsTag = consumes<reco::TrackDeDxHitsCollection>(iConfig.getParameter<edm::InputTag>("stripDeDxHits"));

  usePixel = iConfig.getParameter<bool>("UsePixel");
  useStrip = iConfig.getParameter<bool>("UseStrip");
  meVperADCPixel = iConfig.getParameter<double>("MeVperADCPixel");
  meVperADCStrip = iConfig.getParameter<double>("MeVperADCStrip");

  shapetest = iConfig.getParameter<bool>("ShapeTest");
  useCalibration = iConfig.getParameter<bool>("UseCalibration");
  m_calibrationPath = iConfig.getParameter<string>("calibrationPath");

  if (!usePixel && !useStrip)
    edm::LogWarning("DeDxHitsProducer")
        << "Pixel Hits AND Strip Hits will not be used to estimate dEdx --> BUG, Please Update the config file";
}

DeDxEstimatorProducer::~DeDxEstimatorProducer() {}

// ------------ method called once each job just before starting event loop  ------------
void DeDxEstimatorProducer::beginRun(edm::Run const& run, const edm::EventSetup& iSetup) {
  tkGeom = &iSetup.getData(tkGeomToken);
  if (useCalibration && calibGains.empty()) {
    m_off = tkGeom->offsetDU(GeomDetEnumerators::PixelBarrel);  //index start at the first pixel

    deDxTools::makeCalibrationMap(m_calibrationPath, *tkGeom, calibGains, m_off);
  }

  m_estimator->beginRun(run, iSetup);
}

void DeDxEstimatorProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  auto trackDeDxEstimateAssociation = std::make_unique<ValueMap<DeDxData>>();
  ValueMap<DeDxData>::Filler filler(*trackDeDxEstimateAssociation);

  edm::Handle<reco::TrackCollection> trackCollectionHandle;
  iEvent.getByToken(m_tracksTag, trackCollectionHandle);
  const auto& pixelDeDxHits = iEvent.getHandle(m_pixelDeDxHitsTag);
  const auto& stripDeDxHits = iEvent.getHandle(m_stripDeDxHitsTag);

  std::vector<DeDxData> dedxEstimate(trackCollectionHandle->size());

  for (unsigned int j = 0; j < trackCollectionHandle->size(); j++) {
    const reco::TrackRef track = reco::TrackRef(trackCollectionHandle, j);

    int NClusterSaturating = 0;
    DeDxHitCollection dedxHits;

    dedxHits.reserve(track->recHitsSize() / 2);
    if (useDeDxHits) {
      if (usePixel)
        dedxHits = (*pixelDeDxHits)[track];
      if (useStrip) {
        const auto& hits = (*stripDeDxHits)[track];
        dedxHits.insert(dedxHits.end(), hits.begin(), hits.end());
      }
    } else {
      auto const& trajParams = track->extra()->trajParams();
      assert(trajParams.size() == track->recHitsSize());
      auto hb = track->recHitsBegin();
      dedxHits.reserve(track->recHitsSize() / 2);
      for (unsigned int h = 0; h < track->recHitsSize(); h++) {
        auto recHit = *(hb + h);
        if (!trackerHitRTTI::isFromDet(*recHit))
          continue;

        auto trackDirection = trajParams[h].direction();
        float cosine = trackDirection.z() / trackDirection.mag();
        processHit(recHit, track->p(), cosine, dedxHits, NClusterSaturating);
      }
    }

    sort(dedxHits.begin(), dedxHits.end(), less<DeDxHit>());
    std::pair<float, float> val_and_error = m_estimator->dedx(dedxHits);

    //WARNING: Since the dEdX Error is not properly computed for the moment
    //It was decided to store the number of saturating cluster in that dataformat
    val_and_error.second = NClusterSaturating;
    dedxEstimate[j] = DeDxData(val_and_error.first, val_and_error.second, dedxHits.size());
  }
  ///////////////////////////////////////

  filler.insert(trackCollectionHandle, dedxEstimate.begin(), dedxEstimate.end());

  // fill the association map and put it into the event
  filler.fill();
  iEvent.put(std::move(trackDeDxEstimateAssociation));
}

void DeDxEstimatorProducer::processHit(const TrackingRecHit* recHit,
                                       float trackMomentum,
                                       float& cosine,
                                       reco::DeDxHitCollection& dedxHits,
                                       int& NClusterSaturating) {
  auto const& thit = static_cast<BaseTrackerRecHit const&>(*recHit);
  if (!thit.isValid())
    return;

  auto const& clus = thit.firstClusterRef();
  if (!clus.isValid())
    return;

  if (clus.isPixel()) {
    if (!usePixel)
      return;

    const auto* detUnit = recHit->detUnit();
    if (detUnit == nullptr)
      detUnit = tkGeom->idToDet(thit.geographicalId());
    float pathLen = detUnit->surface().bounds().thickness() / fabs(cosine);
    float chargeAbs = clus.pixelCluster().charge();
    float charge = meVperADCPixel * chargeAbs / pathLen;
    dedxHits.push_back(DeDxHit(charge, trackMomentum, pathLen, thit.geographicalId()));
  } else if (clus.isStrip() && !thit.isMatched()) {
    if (!useStrip)
      return;

    const auto* detUnit = recHit->detUnit();
    if (detUnit == nullptr)
      detUnit = tkGeom->idToDet(thit.geographicalId());
    int NSaturating = 0;
    float pathLen = detUnit->surface().bounds().thickness() / fabs(cosine);
    float chargeAbs = deDxTools::getCharge(&(clus.stripCluster()), NSaturating, *detUnit, calibGains, m_off);
    float charge = meVperADCStrip * chargeAbs / pathLen;
    if (!shapetest || (shapetest && deDxTools::shapeSelection(clus.stripCluster()))) {
      dedxHits.push_back(DeDxHit(charge, trackMomentum, pathLen, thit.geographicalId()));
      if (NSaturating > 0)
        NClusterSaturating++;
    }
  } else if (clus.isStrip() && thit.isMatched()) {
    if (!useStrip)
      return;
    const SiStripMatchedRecHit2D* matchedHit = dynamic_cast<const SiStripMatchedRecHit2D*>(recHit);
    if (!matchedHit)
      return;
    const GluedGeomDet* gdet = static_cast<const GluedGeomDet*>(matchedHit->det());
    if (gdet == nullptr)
      gdet = static_cast<const GluedGeomDet*>(tkGeom->idToDet(thit.geographicalId()));

    auto& detUnitM = *(gdet->monoDet());
    int NSaturating = 0;
    float pathLen = detUnitM.surface().bounds().thickness() / fabs(cosine);
    float chargeAbs = deDxTools::getCharge(&(matchedHit->monoCluster()), NSaturating, detUnitM, calibGains, m_off);
    float charge = meVperADCStrip * chargeAbs / pathLen;
    if (!shapetest || (shapetest && deDxTools::shapeSelection(matchedHit->monoCluster()))) {
      dedxHits.push_back(DeDxHit(charge, trackMomentum, pathLen, matchedHit->monoId()));
      if (NSaturating > 0)
        NClusterSaturating++;
    }

    auto& detUnitS = *(gdet->stereoDet());
    NSaturating = 0;
    pathLen = detUnitS.surface().bounds().thickness() / fabs(cosine);
    chargeAbs = deDxTools::getCharge(&(matchedHit->stereoCluster()), NSaturating, detUnitS, calibGains, m_off);
    charge = meVperADCStrip * chargeAbs / pathLen;
    if (!shapetest || (shapetest && deDxTools::shapeSelection(matchedHit->stereoCluster()))) {
      dedxHits.push_back(DeDxHit(charge, trackMomentum, pathLen, matchedHit->stereoId()));
      if (NSaturating > 0)
        NClusterSaturating++;
    }
  }
}

//define this as a plug-in
DEFINE_FWK_MODULE(DeDxEstimatorProducer);
