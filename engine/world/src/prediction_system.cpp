#include "next/streaming/prediction_system.h"
#include "next/foundation/logger.h"
#include <algorithm>

namespace Next {
namespace Streaming {

// ===== Prediction System Implementation =====

PredictionSystem::PredictionSystem()
    : currentVelocity_(0.0f, 0.0f, 0.0f)
    , smoothedVelocity_(0.0f, 0.0f, 0.0f)
    , currentAcceleration_(0.0f, 0.0f, 0.0f)
    , elapsedTime_(0.0f)
    , totalPredictions_(0)
    , correctPredictions_(0)
    , initialized_(false)
{
}

PredictionSystem::~PredictionSystem() {
    Shutdown();
}

bool PredictionSystem::Initialize(const PredictionSystemConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("PredictionSystem already initialized");
        return true;
    }

    config_ = config;
    elapsedTime_ = 0.0f;

    // Note: std::deque doesn't have reserve(), it's already efficient
    // cameraHistory_.reserve(config_.maxHistorySamples);

    // Reset statistics
    stats_ = Statistics();
    totalPredictions_ = 0;
    correctPredictions_ = 0;

    NEXT_LOG_INFO("PredictionSystem initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Method: %u", static_cast<uint32_t>(config_.method));
    NEXT_LOG_INFO("  Prediction time horizon: %.2f seconds", config_.predictionTimeHorizon);
    NEXT_LOG_INFO("  Prediction samples: %u", config_.predictionSamples);

    initialized_ = true;
    return true;
}

void PredictionSystem::Update(const Vec3& cameraPosition, const Vec3& cameraDirection, float deltaTime, uint64_t frameIndex) {
    if (!initialized_) {
        return;
    }

    // Add camera sample to history
    CameraSample sample;
    sample.position = cameraPosition;
    sample.direction = cameraDirection;
    sample.velocity = currentVelocity_;
    sample.timestamp = elapsedTime_;
    sample.frameIndex = frameIndex;

    AddCameraSample(sample);
    latestSample_ = sample;

    // Update position and velocity
    currentPosition_ = cameraPosition;
    currentDirection_ = cameraDirection;

    UpdateVelocity(cameraPosition, deltaTime);

    // Update statistics
    stats_.averageConfidence = GetAverageConfidence();

    elapsedTime_ += deltaTime;
}

std::vector<PredictionResult> PredictionSystem::PredictFuturePositions(uint32_t sampleCount) const {
    std::vector<PredictionResult> results;
    results.reserve(sampleCount);

    uint32_t samples = sampleCount > 0 ? sampleCount : config_.predictionSamples;
    float timeStep = config_.predictionTimeHorizon / samples;

    for (uint32_t i = 0; i < samples; ++i) {
        float time = timeStep * i;
        PredictionResult result;
        result.predictedPosition = PredictHybrid(time);
        result.predictedDirection = currentDirection_;
        result.confidence = CalculateConfidence(result);
        result.timeHorizon = time;
        // TODO: Calculate cells that will be needed

        results.push_back(result);
    }

    return results;
}

PredictionResult PredictionSystem::PredictAtTime(float timeInSeconds) const {
    PredictionResult result;
    result.predictedPosition = PredictHybrid(timeInSeconds);  // Use PredictHybrid instead of missing method
    result.predictedDirection = currentDirection_;
    result.confidence = CalculateConfidence(result);
    result.timeHorizon = timeInSeconds;

    return result;
}

std::vector<PrefetchRequest> PredictionSystem::GeneratePrefetchRequests() const {
    std::vector<PrefetchRequest> requests;

    // Predict future positions
    std::vector<PredictionResult> predictions = PredictFuturePositions(config_.predictionSamples);

    for (const auto& prediction : predictions) {
        if (prediction.confidence >= config_.minConfidence) {
            // Generate requests for cells along predicted path
            std::vector<CellCoord> cells = PredictCellsAtPath(
                prediction.predictedPosition,
                prediction.predictedDirection,
                prediction.timeHorizon
            );

            for (const CellCoord& coord : cells) {
                PrefetchRequest request;
                request.coord = coord;
                request.priority = prediction.confidence;
                request.timeToLoad = prediction.timeHorizon;
                request.frameIndex = 0;  // TODO: Track current frame

                requests.push_back(request);
            }
        }
    }

    return requests;
}

void PredictionSystem::ClearPrefetchRequests() {
    // TODO: Implement request clearing
}

void PredictionSystem::AddCameraSample(const CameraSample& sample) {
    cameraHistory_.push_back(sample);

    if (cameraHistory_.size() > config_.maxHistorySamples) {
        cameraHistory_.pop_front();
    }

    // Prune old samples
    PruneHistory();
}

const CameraSample* PredictionSystem::GetLatestSample() const {
    if (cameraHistory_.empty()) {
        return nullptr;
    }
    return &cameraHistory_.back();
}

std::vector<CameraSample> PredictionSystem::GetRecentSamples(uint32_t count) const {
    std::vector<CameraSample> samples;

    uint32_t numSamples = std::min(static_cast<uint32_t>(cameraHistory_.size()), count);
    samples.reserve(numSamples);

    auto it = cameraHistory_.end();
    for (uint32_t i = 0; i < numSamples; ++i) {
        --it;
        samples.push_back(*it);
    }

    return samples;
}

float PredictionSystem::CalculateConfidence(const PredictionResult& result) const {
    return CalculatePredictionConfidence(result.predictedPosition);
}

float PredictionSystem::GetAverageConfidence() const {
    // TODO: Implement confidence tracking
    return 0.8f;
}

PredictionSystem::Statistics PredictionSystem::GetStatistics() const {
    return stats_;
}

void PredictionSystem::ResetStatistics() {
    stats_ = Statistics();
    totalPredictions_ = 0;
    correctPredictions_ = 0;
}

void PredictionSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    cameraHistory_.clear();

    initialized_ = false;
    NEXT_LOG_INFO("PredictionSystem shutdown complete");
}

// ===== Private Methods =====

Vec3 PredictionSystem::PredictLinear(float timeInSeconds) const {
    return currentPosition_ + currentVelocity_ * timeInSeconds;
}

Vec3 PredictionSystem::PredictVelocityBased(float timeInSeconds) const {
    return currentPosition_ + smoothedVelocity_ * timeInSeconds;
}

Vec3 PredictionSystem::PredictAccelerationBased(float timeInSeconds) const {
    // p = p0 + v0*t + 0.5*a*t^2
    return currentPosition_ + currentVelocity_ * timeInSeconds + currentAcceleration_ * 0.5f * timeInSeconds * timeInSeconds;
}

Vec3 PredictionSystem::PredictCurveBased(float timeInSeconds) const {
    CurveCoefficients coeffs = FitCurveToHistory();
    float t = timeInSeconds;

    // Quadratic: p(t) = a*t^2 + b*t + c
    return coeffs.a * t * t + coeffs.b * t + coeffs.c;
}

Vec3 PredictionSystem::PredictHybrid(float timeInSeconds) const {
    // Use different methods based on time horizon
    if (timeInSeconds < 0.5f) {
        return PredictVelocityBased(timeInSeconds);
    } else if (timeInSeconds < 1.5f) {
        return PredictAccelerationBased(timeInSeconds);
    } else {
        return PredictCurveBased(timeInSeconds);
    }
}

PredictionSystem::CurveCoefficients PredictionSystem::FitCurveToHistory() const {
    CurveCoefficients coeffs;
    coeffs.c = currentPosition_;  // Current position

    if (cameraHistory_.size() < 3) {
        coeffs.a = Vec3(0.0f, 0.0f, 0.0f);
        coeffs.b = currentVelocity_;
        return coeffs;
    }

    // TODO: Implement proper curve fitting (least squares)
    coeffs.a = currentAcceleration_ * 0.5f;
    coeffs.b = currentVelocity_;

    return coeffs;
}

void PredictionSystem::UpdateVelocity(const Vec3& newPosition, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    Vec3 diff = newPosition - currentPosition_;
    Vec3 newVelocity = diff * (1.0f / deltaTime);  // Manual division to avoid operator/

    // Calculate acceleration
    UpdateAcceleration(newVelocity, deltaTime);

    currentVelocity_ = newVelocity;

    // Apply smoothing
    if (config_.useVelocitySmoothing) {
        smoothedVelocity_ = smoothedVelocity_ * (1.0f - config_.velocitySmoothingFactor) +
                           newVelocity * config_.velocitySmoothingFactor;
    } else {
        smoothedVelocity_ = newVelocity;
    }
}
void PredictionSystem::UpdateAcceleration(const Vec3& newVelocity, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    Vec3 velocityDiff = newVelocity - currentVelocity_;
    currentAcceleration_ = velocityDiff * (1.0f / deltaTime);  // Manual division
}

void PredictionSystem::PruneHistory() {
    if (cameraHistory_.empty()) {
        return;
    }

    float currentTime = cameraHistory_.back().timestamp;
    float maxAge = config_.historyDuration;

    while (!cameraHistory_.empty() && (currentTime - cameraHistory_.front().timestamp) > maxAge) {
        cameraHistory_.pop_front();
    }
}

Vec3 PredictionSystem::CalculateAverageVelocity() const {
    if (cameraHistory_.empty()) {
        return currentVelocity_;
    }

    Vec3 sum(0.0f, 0.0f, 0.0f);
    for (const auto& sample : cameraHistory_) {
        sum = sum + sample.velocity;
    }

    return sum * (1.0f / cameraHistory_.size());
}

std::vector<CellCoord> PredictionSystem::PredictCellsAtPath(const Vec3& position, const Vec3& direction, float timeHorizon) const {
    std::vector<CellCoord> cells;

    // TODO: Implement proper cell prediction along path
    // For now, just return current cell
    CellCoord currentCell;
    currentCell.x = static_cast<int32_t>(std::floor(position.x / 64.0f));
    currentCell.z = static_cast<int32_t>(std::floor(position.z / 64.0f));

    cells.push_back(currentCell);

    return cells;
}

float PredictionSystem::CalculateTimeToCell(const CellCoord& coord, const Vec3& position, const Vec3& velocity) const {
    // TODO: Implement proper time-to-cell calculation
    return 1.0f;
}

float PredictionSystem::CalculatePredictionConfidence(const Vec3& predictedPosition) const {
    // Confidence decays with prediction time
    float confidence = 1.0f - (config_.confidenceDecay * config_.predictionTimeHorizon);
    return std::max(0.0f, std::min(1.0f, confidence));
}

bool PredictionSystem::IsPredictionReliable(float confidence) const {
    return confidence >= config_.minConfidence;
}

} // namespace Streaming
} // namespace Next
