#include "RecoTracker/FinalTrackSelectors/interface/alpaka/TrackFeaturesDeviceCollection.h"
#include "RecoTracker/FinalTrackSelectors/interface/alpaka/TrackScoresDeviceCollection.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackFeaturesSoA.h"

#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

#include "DataFormats/Portable/interface/PortableHostCollection.h"

#include "PhysicsTools/PyTorchAlpaka/interface/TensorCollection.h"
#include "PhysicsTools/PyTorchAlpaka/interface/alpaka/AlpakaModel.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class TrackTorchClassifierAlpaka : public stream::EDProducer<> {
  public:
    TrackTorchClassifierAlpaka(const edm::ParameterSet& iConfig)
        : EDProducer<>(iConfig),
          features_token_(consumes(iConfig.getParameter<edm::InputTag>("features"))),
          scores_device_token_{produces()},
          scores_host_token_{produces("scores").template produces<std::vector<float>>()},
          model_(iConfig.getParameter<edm::FileInPath>("modelPath").fullPath()) {
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::FileInPath>("modelPath", edm::FileInPath("RecoTracker/FinalTrackSelectors/data/best_model.pt"));
      desc.add<edm::InputTag>("features", edm::InputTag("hltInitialStepTrackFeatureExtractor"));
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(device::Event& iEvent, const device::EventSetup& iSetup) override {
      const auto& features = iEvent.get(features_token_);
      const auto batch_size = features.const_view().metadata().size();
      
      auto scores_device = TrackScoresDeviceCollection(batch_size, iEvent.queue());

      auto input_records = features.const_view().records();
      auto output_records = scores_device.view().records();

      cms::torch::alpakatools::TensorCollection<Queue> inputs(batch_size);
      inputs.add<TrackFeaturesSoA>("features",
                                    input_records.pt(),
                                    input_records.innerMomentumX(),
                                    input_records.innerMomentumY(),
                                    input_records.innerMomentumZ(),
                                    input_records.innerMomentumRho(),
                                    input_records.outerMomentumX(),
                                    input_records.outerMomentumY(),
                                    input_records.outerMomentumZ(),
                                    input_records.outerMomentumRho(),
                                    input_records.ptError(),
                                    input_records.dxyBestVertex(),
                                    input_records.dzBestVertex(),
                                    input_records.dxyBeamSpot(),
                                    input_records.dzBeamSpot(),
                                    input_records.dxyError(),
                                    input_records.dzError(),
                                    input_records.normalizedChi2(),
                                    input_records.eta(),
                                    input_records.phi(),
                                    input_records.etaError(),
                                    input_records.phiError(),
                                    input_records.ndof(),
                                    input_records.lostInnerHits(),
                                    input_records.lostOuterHits(),
                                    input_records.layersOffInner(),
                                    input_records.layersOffOuter(),
                                    input_records.layersWithoutMeas(),
                                    input_records.validPixelHits(),
                                    input_records.validStripHits());

      cms::torch::alpakatools::TensorCollection<Queue> outputs(batch_size);
      outputs.add<TrackScoresSoA>("scores", output_records.score());

      model_.forward(iEvent.queue(), inputs, outputs);
      
      // Copy scores to host for CPU consumption
      PortableHostCollection<TrackScoresSoA> scores_host(batch_size);
      alpaka::memcpy(iEvent.queue(), scores_host.buffer(), scores_device.const_buffer());
      alpaka::wait(iEvent.queue());
      
      // Convert to std::vector<float> for easy CPU access
      std::vector<float> scores_vec;
      scores_vec.reserve(batch_size);
      auto scores_view = scores_host.const_view();
      for (int i = 0; i < batch_size; ++i) {
        scores_vec.push_back(scores_view[i].score());
      }
      
      iEvent.emplace(scores_device_token_, std::move(scores_device));
      iEvent.put(scores_host_token_, std::make_unique<std::vector<float>>(std::move(scores_vec)));
    }

  private:
    const device::EDGetToken<TrackFeaturesDeviceCollection> features_token_;
    const device::EDPutToken<TrackScoresDeviceCollection> scores_device_token_;
    const edm::EDPutTokenT<std::vector<float>> scores_host_token_;
    torch::AlpakaModel model_;
  };

}

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(TrackTorchClassifierAlpaka);
