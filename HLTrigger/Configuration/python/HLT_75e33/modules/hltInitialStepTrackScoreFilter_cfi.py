import FWCore.ParameterSet.Config as cms

hltInitialStepTrackScoreFilter = cms.EDProducer("TrackScoreFilter",
    src = cms.InputTag("hltInitialStepTracks"),
    scores = cms.InputTag("hltInitialStepTrackTorchClassifier", "scores"),
    minScore = cms.double(0.056)
)
