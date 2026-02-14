#include "OpenGLPianoRollComponent.h"
#include "BasicPitchConstants.h"
#include <algorithm>
#include <cmath>

using namespace juce::gl;

OpenGLPianoRollComponent::OpenGLPianoRollComponent()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(false);
    openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    openGLContext.attachTo(*this);

    for (size_t i = 0; i < noteBaseColours.size(); ++i)
    {
        const float hue = std::fmod(static_cast<float>(i) * 0.07f, 1.0f);
        const auto colour = juce::Colour::fromHSV(hue, 0.65f, 0.92f, 1.0f);
        noteBaseColours[i] = { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue() };
    }

    startTimerHz(10);
}

OpenGLPianoRollComponent::~OpenGLPianoRollComponent()
{
    openGLContext.detach();
}

void OpenGLPianoRollComponent::noteOn(int note, float velocity, double timeSeconds)
{
    if (note < 0 || note >= static_cast<int>(activeNotes.size()))
        return;

    juce::SpinLock::ScopedLockType lock(dataLock);

    const double wallNow = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (!hasWallToNoteOffset || timeSeconds + 0.1 < currentTimeSeconds)
    {
        wallToNoteTimeOffsetSeconds = wallNow - timeSeconds;
        hasWallToNoteOffset = true;
    }
    currentTimeSeconds = juce::jmax(currentTimeSeconds, timeSeconds);

    auto& slot = activeNotes[static_cast<size_t>(note)];
    if (!slot.active)
    {
        slot.active = true;
        slot.startTime = timeSeconds;
        slot.velocity = velocity;
    }
    else
    {
        slot.velocity = juce::jmax(slot.velocity, velocity);
    }

    lastNoteEventTime = timeSeconds;
    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::noteOff(int note, double timeSeconds)
{
    if (note < 0 || note >= static_cast<int>(activeNotes.size()))
        return;

    juce::SpinLock::ScopedLockType lock(dataLock);

    const double wallNow = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (!hasWallToNoteOffset || timeSeconds + 0.1 < currentTimeSeconds)
    {
        wallToNoteTimeOffsetSeconds = wallNow - timeSeconds;
        hasWallToNoteOffset = true;
    }
    currentTimeSeconds = juce::jmax(currentTimeSeconds, timeSeconds);

    auto& slot = activeNotes[static_cast<size_t>(note)];
    if (!slot.active)
        return;

    NoteBar bar;
    bar.note = note;
    bar.velocity = slot.velocity;
    bar.startTime = slot.startTime;
    bar.endTime = timeSeconds;
    bar.voiceId = 0;
    noteHistory.push_back(bar);

    slot.active = false;
    slot.velocity = 0.0f;
    lastNoteEventTime = timeSeconds;
    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::clear()
{
    juce::SpinLock::ScopedLockType lock(dataLock);
    noteHistory.clear();
    for (auto& note : activeNotes)
        note = ActiveNote{};
    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::setTimeWindowSeconds(double seconds)
{
    juce::SpinLock::ScopedLockType lock(dataLock);
    timeWindowSeconds = juce::jlimit(2.0, 20.0, seconds);
    openGLContext.triggerRepaint();
}

double OpenGLPianoRollComponent::getLastNoteEventTimeSeconds() const
{
    juce::SpinLock::ScopedLockType lock(dataLock);
    return lastNoteEventTime;
}

void OpenGLPianoRollComponent::setFrozen(bool frozen)
{
    if (isFrozen == frozen)
        return;

    juce::SpinLock::ScopedLockType lock(dataLock);
    isFrozen = frozen;
    if (isFrozen)
        freezeTimeSeconds = currentTimeSeconds;
    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::setScrollEnabled(bool enabled)
{
    if (scrollEnabled == enabled)
        return;

    juce::SpinLock::ScopedLockType lock(dataLock);
    scrollEnabled = enabled;
    if (!scrollEnabled)
        pausedViewTimeSeconds = currentTimeSeconds;
    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::resized()
{
    juce::SpinLock::ScopedLockType lock(dataLock);
    viewWidth.store(static_cast<float>(getWidth()));
    viewHeight.store(static_cast<float>(getHeight()));
    staticGeometryDirty.store(true);
    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::mouseMove(const juce::MouseEvent& event)
{
    updateTooltipForPoint(event.position);
}

void OpenGLPianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    grabKeyboardFocus();
    juce::SpinLock::ScopedLockType lock(dataLock);
    isFrozen = !isFrozen;
    if (isFrozen)
        freezeTimeSeconds = currentTimeSeconds;
    openGLContext.triggerRepaint();
}

bool OpenGLPianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        juce::SpinLock::ScopedLockType lock(dataLock);
        isFrozen = !isFrozen;
        if (isFrozen)
            freezeTimeSeconds = currentTimeSeconds;
        openGLContext.triggerRepaint();
        return true;
    }
    return false;
}

void OpenGLPianoRollComponent::timerCallback()
{
    {
        juce::SpinLock::ScopedLockType lock(dataLock);
        if (isFrozen)
            return;

        if (hasWallToNoteOffset)
        {
            const double wallNow = juce::Time::getMillisecondCounterHiRes() * 0.001;
            currentTimeSeconds = juce::jmax(currentTimeSeconds, wallNow - wallToNoteTimeOffsetSeconds);
        }

        if (scrollEnabled)
        {
            pausedViewTimeSeconds = currentTimeSeconds;
            pruneOldNotes(currentTimeSeconds);
        }
    }

    openGLContext.triggerRepaint();
}

void OpenGLPianoRollComponent::newOpenGLContextCreated()
{
    const juce::String vertexShader = R"(
        attribute vec2 position;
        attribute vec4 colour;
        attribute vec2 localPos;
        attribute vec2 rectSize;
        attribute float cornerRadius;
        uniform vec2 screenSize;
        varying vec4 vColour;
        varying vec2 vLocalPos;
        varying vec2 vRectSize;
        varying float vCornerRadius;

        void main()
        {
            vec2 clip = (position / screenSize) * 2.0 - 1.0;
            gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
            vColour = colour;
            vLocalPos = localPos;
            vRectSize = rectSize;
            vCornerRadius = cornerRadius;
        }
    )";

    const juce::String fragmentShader = R"(
        varying vec4 vColour;
        varying vec2 vLocalPos;
        varying vec2 vRectSize;
        varying float vCornerRadius;

        void main()
        {
            float alpha = 1.0;
            if (vCornerRadius > 0.0)
            {
                vec2 halfSize = vRectSize * 0.5;
                vec2 p = vLocalPos * vRectSize - halfSize;
                vec2 q = abs(p) - (halfSize - vec2(vCornerRadius));
                float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vCornerRadius;
                alpha = clamp(1.0 - smoothstep(0.0, 1.0, dist), 0.0, 1.0);
            }
            gl_FragColor = vec4(vColour.rgb, vColour.a * alpha);
        }
    )";

    shader.reset();
    std::unique_ptr<juce::OpenGLShaderProgram> newShader(new juce::OpenGLShaderProgram(openGLContext));
    if (newShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(vertexShader))
        && newShader->addFragmentShader(juce::OpenGLHelpers::translateFragmentShaderToV3(fragmentShader))
        && newShader->link())
    {
        shader = std::move(newShader);
        positionAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "position");
        colourAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "colour");
        localAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "localPos");
        sizeAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "rectSize");
        radiusAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "cornerRadius");
        screenSizeUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shader, "screenSize");
    }

    openGLContext.extensions.glGenBuffers(1, &vbo);
    openGLContext.extensions.glGenVertexArrays(1, &vao);
}

void OpenGLPianoRollComponent::renderOpenGL()
{
    if (shader == nullptr)
        return;

    const float width = viewWidth.load();
    const float height = viewHeight.load();
    if (width <= 0.0f || height <= 0.0f)
        return;

    const float scale = static_cast<float>(openGLContext.getRenderingScale());
    const int fbWidth = static_cast<int>(std::lround(width * scale));
    const int fbHeight = static_cast<int>(std::lround(height * scale));

    juce::OpenGLHelpers::clear(juce::Colour(0xFF10151A));
    glViewport(0, 0, static_cast<GLsizei>(fbWidth), static_cast<GLsizei>(fbHeight));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (staticGeometryDirty.load())
    {
        juce::SpinLock::ScopedLockType lock(dataLock);
        rebuildStaticGeometry();
        staticGeometryDirty.store(false);
    }

    juce::SpinLock::ScopedTryLockType lock(dataLock);
    if (!lock.isLocked())
        return;

    const juce::Rectangle<float> area(0.0f, 0.0f, width, height);
    const double currentTime = isFrozen ? freezeTimeSeconds
                                        : (scrollEnabled ? currentTimeSeconds : pausedViewTimeSeconds);

    frameVertices.clear();
    frameVertices.reserve(staticVertices.size() + (noteHistory.size() + activeNotes.size()) * 6);
    frameVertices.insert(frameVertices.end(), staticVertices.begin(), staticVertices.end());

    auto addEvent = [&](const NoteBar& event, bool isActive)
    {
        const double endTime = isActive ? currentTime : event.endTime;
        const double duration = juce::jmax(0.0, endTime - event.startTime);
        if (duration <= 0.0)
            return;

        auto rect = noteRectForEvent({ event.note, event.velocity, event.startTime, endTime, event.voiceId }, currentTime, area);
        if (!rect.intersects(area))
            return;

        const float alpha = juce::jlimit(0.25f, 0.95f, event.velocity);
        const auto& base = noteBaseColours[static_cast<size_t>(event.note)];
        addRect(frameVertices, rect, base.x, base.y, base.z, alpha, 3.0f);
    };

    for (const auto& event : noteHistory)
        addEvent(event, false);

    for (size_t i = 0; i < activeNotes.size(); ++i)
    {
        const auto& note = activeNotes[i];
        if (!note.active)
            continue;

        NoteBar bar;
        bar.note = static_cast<int>(i);
        bar.velocity = note.velocity;
        bar.startTime = note.startTime;
        bar.endTime = currentTime;
        addEvent(bar, true);
    }

    shader->use();
    if (screenSizeUniform != nullptr)
        screenSizeUniform->set(width, height);

    openGLContext.extensions.glBindVertexArray(vao);
    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const auto dataSize = static_cast<GLsizeiptr>(frameVertices.size() * sizeof(Vertex));
    openGLContext.extensions.glBufferData(GL_ARRAY_BUFFER, dataSize, nullptr, GL_DYNAMIC_DRAW);
    if (dataSize > 0)
        openGLContext.extensions.glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, frameVertices.data());

    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    auto enableAttrib = [&](juce::OpenGLShaderProgram::Attribute* attrib, GLint size, size_t offset)
    {
        if (attrib == nullptr)
            return;
        openGLContext.extensions.glVertexAttribPointer(static_cast<GLuint>(attrib->attributeID), size, GL_FLOAT, GL_FALSE,
                                                       stride, reinterpret_cast<const void*>(offset));
        openGLContext.extensions.glEnableVertexAttribArray(static_cast<GLuint>(attrib->attributeID));
    };

    enableAttrib(positionAttribute.get(), 2, offsetof(Vertex, x));
    enableAttrib(colourAttribute.get(), 4, offsetof(Vertex, r));
    enableAttrib(localAttribute.get(), 2, offsetof(Vertex, u));
    enableAttrib(sizeAttribute.get(), 2, offsetof(Vertex, w));
    enableAttrib(radiusAttribute.get(), 1, offsetof(Vertex, radius));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frameVertices.size()));

    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
    openGLContext.extensions.glBindVertexArray(0);

    if (auto glRenderer = juce::createOpenGLGraphicsContext(openGLContext, fbWidth, fbHeight))
    {
        juce::Graphics g(*glRenderer);
        g.setColour(juce::Colour(0xFF4A5862));
        g.setFont(11.0f);

        const int minNote = MIN_MIDI_NOTE;
        const int maxNote = MAX_MIDI_NOTE;
        const int noteCount = maxNote - minNote + 1;
        const float noteHeight = area.getHeight() / static_cast<float>(noteCount);

        for (int note = minNote; note <= maxNote; ++note)
        {
            const bool isOctave = (note % 12) == 0;
            if (!isOctave)
                continue;

            const float y = noteToY(note, area);
            g.drawText(noteName(note), 6.0f, y - noteHeight * 0.5f, 40.0f, noteHeight,
                       juce::Justification::centredLeft);
        }

        if (isFrozen)
        {
            g.setColour(juce::Colour(0xFFDAA632));
            g.setFont(12.0f);
            g.drawText("Frozen", area.withY(area.getBottom() - 18.0f).withHeight(18.0f).withWidth(70.0f).withX(area.getRight() - 70.0f),
                       juce::Justification::centredRight);
        }
    }
}

void OpenGLPianoRollComponent::openGLContextClosing()
{
    shader.reset();
    positionAttribute.reset();
    colourAttribute.reset();
    localAttribute.reset();
    sizeAttribute.reset();
    radiusAttribute.reset();
    screenSizeUniform.reset();

    if (vao != 0)
        openGLContext.extensions.glDeleteVertexArrays(1, &vao);
    if (vbo != 0)
        openGLContext.extensions.glDeleteBuffers(1, &vbo);
    vao = 0;
    vbo = 0;
}

void OpenGLPianoRollComponent::pruneOldNotes(double currentTime)
{
    const double minTime = currentTime - timeWindowSeconds;
    noteHistory.erase(std::remove_if(noteHistory.begin(), noteHistory.end(),
                                     [minTime](const NoteBar& event)
                                     {
                                         return event.endTime < minTime;
                                     }),
                      noteHistory.end());
}

void OpenGLPianoRollComponent::updateTooltipForPoint(juce::Point<float> point)
{
    juce::SpinLock::ScopedLockType lock(dataLock);
    const juce::Rectangle<float> area = getLocalBounds().toFloat();
    const double currentTime = isFrozen ? freezeTimeSeconds
                                        : (scrollEnabled ? currentTimeSeconds : pausedViewTimeSeconds);

    auto hitTest = [&](const NoteBar& event, bool isActive) -> bool
    {
        const double endTime = isActive ? currentTime : event.endTime;
        auto rect = noteRectForEvent({ event.note, event.velocity, event.startTime, endTime, event.voiceId }, currentTime, area);
        return rect.contains(point);
    };

    for (const auto& event : noteHistory)
    {
        if (hitTest(event, false))
        {
            const double duration = event.endTime - event.startTime;
            hoverTooltip = juce::String(noteName(event.note))
                + " | " + juce::String(duration, 2) + "s"
                + " | vel " + juce::String(event.velocity, 2);
           #if JUCE_GUI_BASICS
            setTooltip(hoverTooltip);
           #endif
            return;
        }
    }

    for (size_t i = 0; i < activeNotes.size(); ++i)
    {
        const auto& note = activeNotes[i];
        if (!note.active)
            continue;

        NoteBar event;
        event.note = static_cast<int>(i);
        event.velocity = note.velocity;
        event.startTime = note.startTime;
        event.endTime = currentTime;
        if (hitTest(event, true))
        {
            const double duration = currentTime - note.startTime;
            hoverTooltip = juce::String(noteName(event.note))
                + " | " + juce::String(duration, 2) + "s"
                + " | vel " + juce::String(event.velocity, 2);
           #if JUCE_GUI_BASICS
            setTooltip(hoverTooltip);
           #endif
            return;
        }
    }

    hoverTooltip.clear();
   #if JUCE_GUI_BASICS
    setTooltip({});
   #endif
}

void OpenGLPianoRollComponent::rebuildStaticGeometry()
{
    staticVertices.clear();

    const float width = viewWidth.load();
    const float height = viewHeight.load();
    if (width <= 0.0f || height <= 0.0f)
        return;

    const juce::Rectangle<float> area(0.0f, 0.0f, width, height);
    const int minNote = MIN_MIDI_NOTE;
    const int maxNote = MAX_MIDI_NOTE;
    const int noteCount = maxNote - minNote + 1;
    const float noteHeight = area.getHeight() / static_cast<float>(noteCount);

    for (int note = minNote; note <= maxNote; ++note)
    {
        const float y = noteToY(note, area);
        const bool blackKey = isBlackKey(note);
        if (blackKey)
        {
            addRect(staticVertices, area.withY(y).withHeight(noteHeight), juce::Colour(0xFF121A21), 0.0f);
        }

        const bool isOctave = (note % 12) == 0;
        const float thickness = isOctave ? 1.6f : 0.8f;
        addRect(staticVertices,
                { area.getX(), y - thickness * 0.5f, area.getWidth(), thickness },
                isOctave ? juce::Colour(0xFF28323A) : juce::Colour(0xFF1C232A),
                0.0f);
    }

    const float nowX = area.getX() + area.getWidth() * 0.88f;
    addRect(staticVertices,
            { nowX - 1.0f, area.getY(), 2.0f, area.getHeight() },
            juce::Colour(0xFF41515C),
            0.0f);
}

void OpenGLPianoRollComponent::addRect(std::vector<Vertex>& target, juce::Rectangle<float> rect,
                                       juce::Colour colour, float radius) const
{
    if (rect.getWidth() <= 0.0f || rect.getHeight() <= 0.0f)
        return;

    addRect(target, rect,
            colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha(),
            radius);
}

void OpenGLPianoRollComponent::addRect(std::vector<Vertex>& target, juce::Rectangle<float> rect,
                                       float r, float g, float b, float a, float radius) const
{
    if (rect.getWidth() <= 0.0f || rect.getHeight() <= 0.0f)
        return;

    const float x0 = rect.getX();
    const float y0 = rect.getY();
    const float x1 = rect.getRight();
    const float y1 = rect.getBottom();
    const float w = rect.getWidth();
    const float h = rect.getHeight();

    const Vertex v0 { x0, y0, r, g, b, a, 0.0f, 0.0f, w, h, radius };
    const Vertex v1 { x1, y0, r, g, b, a, 1.0f, 0.0f, w, h, radius };
    const Vertex v2 { x1, y1, r, g, b, a, 1.0f, 1.0f, w, h, radius };
    const Vertex v3 { x0, y1, r, g, b, a, 0.0f, 1.0f, w, h, radius };

    target.push_back(v0);
    target.push_back(v1);
    target.push_back(v2);
    target.push_back(v0);
    target.push_back(v2);
    target.push_back(v3);
}

juce::Rectangle<float> OpenGLPianoRollComponent::noteRectForEvent(const NoteBar& noteEvent,
                                                                  double currentTime,
                                                                  const juce::Rectangle<float>& area) const
{
    const float nowX = area.getX() + area.getWidth() * 0.88f;
    const float pixelsPerSecond = area.getWidth() / static_cast<float>(timeWindowSeconds);
    const float xStart = nowX - static_cast<float>(currentTime - noteEvent.startTime) * pixelsPerSecond;
    const float xEnd = nowX - static_cast<float>(currentTime - noteEvent.endTime) * pixelsPerSecond;
    const float left = juce::jmin(xStart, xEnd);
    const float right = juce::jmax(xStart, xEnd);
    const float height = area.getHeight() / static_cast<float>(MAX_MIDI_NOTE - MIN_MIDI_NOTE + 1);
    const float y = noteToY(noteEvent.note, area);
    return { left, y, right - left, height * 0.9f };
}

float OpenGLPianoRollComponent::noteToY(int note, const juce::Rectangle<float>& area) const
{
    const int minNote = MIN_MIDI_NOTE;
    const int maxNote = MAX_MIDI_NOTE;
    const int noteCount = maxNote - minNote + 1;
    const float noteHeight = area.getHeight() / static_cast<float>(noteCount);
    const int offset = note - minNote;
    return area.getBottom() - (offset + 1) * noteHeight;
}

bool OpenGLPianoRollComponent::isBlackKey(int midiNote) const
{
    const int pitchClass = midiNote % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

juce::String OpenGLPianoRollComponent::noteName(int midiNote) const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = midiNote / 12 - 1;
    return juce::String(names[midiNote % 12]) + juce::String(octave);
}
