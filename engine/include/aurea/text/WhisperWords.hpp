#pragma once
#include "aurea/text/Captions.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::text {
// Token text and timing are separate: an unaligned subtoken still belongs to
// the word. Overlapping estimates must not delete recognized speech.
class WhisperWords {
public:
    WhisperWords(std::vector<CaptionWord>& output, double offset, double begin, double end)
        : output_(output), offset_(offset), begin_(begin), end_(end) {}
    void token(const std::string& text, double start, double end,
               double segmentStart, double segmentEnd) {
        if (text.empty()) return;
        if (text.find_first_of(" \r\n\t") == 0) flush();
        const auto first = text.find_first_not_of(" \r\n\t");
        if (first == std::string::npos) return;
        const bool timed = std::isfinite(start) && std::isfinite(end) && start >= 0 && end >= start;
        if (word_.text.empty()) word_.start = offset_ + (timed ? start : segmentStart);
        word_.text += text.substr(first);
        word_.end = std::max(word_.end, offset_ + (timed ? end : segmentEnd));
    }
    void flush() {
        const double midpoint = (word_.start + word_.end) * .5;
        if (!word_.text.empty() && midpoint >= begin_ && midpoint < end_) {
            word_.start = std::max(begin_, word_.start);
            word_.end = std::min(end_, std::max(word_.start + .01, word_.end));
            if (!output_.empty() && output_.back().end > word_.start) {
                // Split the overlap, preserving both words and their order.
                auto& previous = output_.back();
                const double boundary = std::clamp((previous.end + word_.start) * .5,
                    previous.start, std::max(previous.start, word_.end));
                previous.end = boundary;
                word_.start = boundary;
            }
            output_.push_back(word_);
        }
        word_ = {};
    }
private:
    std::vector<CaptionWord>& output_;
    double offset_, begin_, end_;
    CaptionWord word_;
};
}
