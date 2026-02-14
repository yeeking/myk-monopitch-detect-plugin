#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

class OpenGLPianoRollComponent : public juce::Component,
                                 private juce::OpenGLRenderer,
                                 private juce::Timer
{
public:
    OpenGLPianoRollComponent();
    ~OpenGLPianoRollComponent() override;

    void noteOn(int note, float velocity, double timeSeconds);
    void noteOff(int note, double timeSeconds);
    void clear();

    void setTimeWindowSeconds(double seconds);
    double getLastNoteEventTimeSeconds() const;
    void setFrozen(bool frozen);
    void setScrollEnabled(bool enabled);

    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct NoteBar
    {
        int note = 0;
        float velocity = 0.0f;
        double startTime = 0.0;
        double endTime = 0.0;
        int voiceId = 0;
    };

    struct ActiveNote
    {
        bool active = false;
        double startTime = 0.0;
        float velocity = 0.0f;
    };

    struct Vertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float radius = 0.0f;
    };

    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void pruneOldNotes(double currentTime);
    void updateTooltipForPoint(juce::Point<float> point);
    void rebuildStaticGeometry();
    void addRect(std::vector<Vertex>& target, juce::Rectangle<float> rect, juce::Colour colour, float radius) const;
    void addRect(std::vector<Vertex>& target, juce::Rectangle<float> rect,
                 float r, float g, float b, float a, float radius) const;
    juce::Rectangle<float> noteRectForEvent(const NoteBar& noteEvent,
                                            double currentTime,
                                            const juce::Rectangle<float>& area) const;
    float noteToY(int note, const juce::Rectangle<float>& area) const;
    bool isBlackKey(int midiNote) const;
    juce::String noteName(int midiNote) const;

    std::array<ActiveNote, 128> activeNotes;
    std::vector<NoteBar> noteHistory;

    double timeWindowSeconds = 8.0;
    double lastNoteEventTime = 0.0;
    double currentTimeSeconds = 0.0;
    double freezeTimeSeconds = 0.0;
    double pausedViewTimeSeconds = 0.0;
    double wallToNoteTimeOffsetSeconds = 0.0;
    bool hasWallToNoteOffset = false;
    bool isFrozen = false;
    bool scrollEnabled = true;

    juce::String hoverTooltip;

    juce::SpinLock dataLock;
    std::atomic<float> viewWidth { 0.0f };
    std::atomic<float> viewHeight { 0.0f };
    std::atomic<bool> staticGeometryDirty { true };

    std::vector<Vertex> staticVertices;
    std::vector<Vertex> frameVertices;
    std::array<juce::Vector3D<float>, 128> noteBaseColours;

    juce::OpenGLContext openGLContext;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> colourAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> localAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> sizeAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> radiusAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> screenSizeUniform;
    GLuint vbo = 0;
    GLuint vao = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGLPianoRollComponent)
};
