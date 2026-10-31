#include "ui/widgets/NuxKnob.h"

#include <algorithm>
#include <cmath>

namespace qtn
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double pi = juce::MathConstants<double>::pi;

/** JUCE Slider's smallestAngleBetween, inlined in mouseDrag@1000897b0.c:L75-L86. */
double smallestAngleBetween (double a1, double a2) noexcept
{
    return std::min ({std::abs (a1 - a2), std::abs (a1 + twoPi - a2), std::abs (a2 + twoPi - a1)});
}
} // namespace

// ---- KnobModel ---------------------------------------------------------------------------------

void KnobModel::setRange (int minimum, int maximum) noexcept
{
    min = std::min (minimum, maximum);
    max = std::max (minimum, maximum);
    current = std::clamp (current, min, max);
}

void KnobModel::setAngles (float start, float end) noexcept
{
    startDegrees = std::clamp (start, 0.0f, 360.0f);
    endDegrees = std::clamp (end, 0.0f, 360.0f);
}

bool KnobModel::setValue (double newValue) noexcept
{
    // Interval 1: floor((v - start) / 1 + 0.5) + start, then clamp
    // (decomp:NuxKnob/setValue@10008a4a0.c:L35-L45).
    const double snapped = std::floor (newValue - min + 0.5) + min;
    const int clamped = static_cast<int> (std::clamp (snapped, static_cast<double> (min), static_cast<double> (max)));
    const bool changed = clamped != current;
    current = clamped;
    return changed;
}

double KnobModel::proportion() const noexcept
{
    if (max == min)
        return 0.0;
    return std::clamp (static_cast<double> (current - min) / (max - min), 0.0, 1.0);
}

double KnobModel::valueFromProportion (double p) const noexcept
{
    return min + (max - min) * std::clamp (p, 0.0, 1.0); // NormalisableRange::convertFrom0to1, skew 1
}

double KnobModel::startRadians() const noexcept
{
    // (deg * (1/180) + 1) * pi (decomp:NuxInstrumentPanel/showElement@100245f50.c:L436-L437).
    return (startDegrees * 0.0055555557f + 1.0f) * juce::MathConstants<float>::pi;
}

double KnobModel::endRadians() const noexcept
{
    return (endDegrees * 0.0055555557f + 1.0f) * juce::MathConstants<float>::pi;
}

int KnobModel::frameIndex() const noexcept
{
    // ceilf(start / 3) + ceil((end - start) / 3 * p); 120 -> 0 (decomp:NuxKnob/paint@10008a0b0.c:L79-L85).
    const int first = static_cast<int> (std::ceil (startDegrees / 3.0f));
    const auto sweep = static_cast<float> ((endDegrees - startDegrees) / 3.0f * proportion());
    int frame = first + static_cast<int> (std::ceil (sweep));
    if (frame == frameCount)
        frame = 0;
    return std::clamp (frame, 0, frameCount - 1);
}

juce::Rectangle<int> KnobModel::frameSourceArea (int frame, int stripWidth) noexcept
{
    // Square frames of stripWidth / 12, 12 per row (paint@10008a0b0.c:L87-L93).
    const int size = stripWidth / framesPerRow;
    return {(frame % framesPerRow) * size, (frame / framesPerRow) * size, size, size};
}

void KnobModel::mouseDown (juce::Point<float> position) noexcept
{
    // Stores the press position and the value; the next drag event is skipped
    // (decomp:NuxKnob/mouseDown@100089740.c:L20-L24).
    downPosition = position;
    proportionAtDown = proportion();
    // Deviation: the original never sets lastAngle (+0xc0) on mouse-down (JUCE's Slider does);
    // we start it at the current value's angle so the first round drag does not jump.
    lastAngle = startRadians() + (endRadians() - startRadians()) * proportionAtDown;
    skipNextDrag = true;
}

double KnobModel::roundDragAngle (juce::Point<float> position, juce::Point<int> size, bool dragged) const noexcept
{
    // JUCE Slider::handleRotaryDrag as compiled into mouseDrag@1000897b0.c:L60-L120, with
    // rotary "stop at end" on (+0x278 = 1) and MouseEvent+0x59 = mouseWasDraggedSinceMouseDown.
    const float dx = position.x - static_cast<float> (size.x / 2);
    const float dy = position.y - static_cast<float> (size.y / 2);
    double angle = std::atan2 (static_cast<double> (dx), static_cast<double> (-dy));
    while (angle < 0.0)
        angle += twoPi;

    const double start = startRadians();
    const double end = endRadians();

    if (dragged)
    {
        if (std::abs (angle - lastAngle) > pi)
            angle += (angle >= lastAngle) ? -twoPi : twoPi;

        return angle >= lastAngle ? std::min (angle, std::max (start, end)) : std::max (angle, std::min (start, end));
    }

    while (angle < start)
        angle += twoPi;
    if (angle > end)
        angle = smallestAngleBetween (angle, start) <= smallestAngleBetween (angle, end) ? start : end;
    return angle;
}

bool KnobModel::mouseDrag (juce::Point<float> position, juce::Point<int> size, bool draggedSinceMouseDown) noexcept
{
    if (skipNextDrag) // mouseDrag@1000897b0.c:L55-L57
    {
        skipNextDrag = false;
        return false;
    }

    if (dragMode == KnobDragMode::round)
    {
        const float dx = position.x - static_cast<float> (size.x / 2);
        const float dy = position.y - static_cast<float> (size.y / 2);
        if (dx * dx + dy * dy <= deadZoneSquared)
            return false;

        const double angle = roundDragAngle (position, size, draggedSinceMouseDown);
        lastAngle = angle;
        const double span = endRadians() - startRadians();
        const double p = span != 0.0 ? (angle - startRadians()) / span : 0.0;
        return setValue (valueFromProportion (p));
    }

    // Straight: ((downY - y) + (x - downX)) * 0.005 added to the proportion at mouse-down (L302).
    const double delta = static_cast<double> ((downPosition.y - position.y) + (position.x - downPosition.x));
    return setValue (valueFromProportion (proportionAtDown + delta * straightDragPerPixel));
}

bool KnobModel::mouseWheel (float deltaY) noexcept
{
    // ceilf(deltaY) when deltaY > 0 else floorf(deltaY) (asm@0x10008a039, mouseWheelMove@10008a030.c).
    const float step = deltaY > 0.0f ? std::ceil (deltaY) : std::floor (deltaY);
    return setValue (current + static_cast<double> (step));
}

bool KnobModel::doubleClick() noexcept
{
    return setValue (defaultValue); // mouseDoubleClick@10008a010.c:L13 (+0xd8)
}

// ---- NuxKnob -----------------------------------------------------------------------------------

NuxKnob::NuxKnob()
{
    model.setDragMode (KnobDragMode::round);
}

void NuxKnob::changed (bool valueChanged, juce::NotificationType notification)
{
    if (! valueChanged)
        return;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange (model.value());
}

void NuxKnob::setValue (int newValue, juce::NotificationType notification)
{
    changed (model.setValue (newValue), notification);
}

void NuxKnob::setRange (int minimum, int maximum)
{
    model.setRange (minimum, maximum);
    repaint();
}

void NuxKnob::setAngles (float startDegrees, float endDegrees)
{
    model.setAngles (startDegrees, endDegrees);
    repaint();
}

void NuxKnob::setFilmstrip (const juce::Image& strip)
{
    frames.clear();
    if (strip.isValid())
    {
        frames.reserve (static_cast<size_t> (KnobModel::frameCount));
        for (int i = 0; i < KnobModel::frameCount; ++i)
            frames.push_back (strip.getClippedImage (KnobModel::frameSourceArea (i, strip.getWidth())));
    }
    repaint();
}

void NuxKnob::paint (juce::Graphics& g)
{
    const auto frame = static_cast<size_t> (model.frameIndex());
    if (frame < frames.size() && frames[frame].isValid())
        g.drawImage (frames[frame], getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

void NuxKnob::mouseDown (const juce::MouseEvent& e)
{
    if (isEnabled()) // the original walks the parents' disabled flags (mouseDown@100089740.c:L15-L19)
        model.mouseDown (e.position);
}

void NuxKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (isEnabled())
        changed (model.mouseDrag (e.position, {getWidth(), getHeight()}, e.mouseWasDraggedSinceMouseDown()),
                 juce::sendNotificationSync);
}

void NuxKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    changed (model.doubleClick(), juce::sendNotificationSync);
}

void NuxKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    changed (model.mouseWheel (wheel.deltaY), juce::sendNotificationSync);
}
} // namespace qtn
