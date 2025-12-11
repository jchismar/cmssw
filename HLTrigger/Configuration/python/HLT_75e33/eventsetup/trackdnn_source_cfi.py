import FWCore.ParameterSet.Config as cms

trackdnn_source = cms.ESSource("EmptyESSource", 
    recordName = cms.string("TfGraphRecord"), 
    firstValid = cms.vuint32(1), 
    iovIsRunNotTime = cms.bool(True)
)

hltInitialStepTrackTorchClassifier = cms.EDProducer("TrackTorchClassifier",
    src = cms.InputTag("hltInitialStepTracks"),
    beamspot = cms.InputTag("hltOnlineBeamSpot"),
    vertices = cms.InputTag("hltPhase2PixelVertices"),
    ignoreVertices = cms.bool(False),
    modelPath = cms.string('RecoTracker/FinalTrackSelectors/data/best_model.pt'),
    batchSize = cms.int32(16),
    minScore = cms.double(0.1)
)
