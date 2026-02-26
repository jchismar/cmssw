import FWCore.ParameterSet.Config as cms

hltInitialStepTrackTorchClassifierOutput = cms.EDProducer("TrackTorchClassifierAlpakaOutput",
    src = cms.InputTag("hltInitialStepTracks"),
    scores = cms.InputTag("hltInitialStepTrackTorchClassifier"),
    features = cms.InputTag("hltInitialStepTrackFeatureExtractor"),
    # minScore = cms.double(0.377), #version17, 15 features, focal loss
    minScore = cms.double(0.058), #version7, 14 features, BCE loss
    dxyThreshold = cms.double(0.5),
    # highDxyMinScore = cms.double(0.267) #ver 17
    highDxyMinScore = cms.double(0.004) #ver 7
)
