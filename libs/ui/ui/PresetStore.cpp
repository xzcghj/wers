#include "PresetStore.h"
#include "SharedText.h"

namespace wers::ui {

namespace {

constexpr const char* kRootTag = "WERS_PRESET";
constexpr const char* kParamTag = "PARAM";
constexpr int kFormatVersion = 1;
constexpr juce::int64 kMaxFileBytes = 1 << 20;

juce::String polish(const char* utf8) { return juce::String::fromUTF8(utf8); }

} // namespace

PresetStore::PresetStore(juce::AudioProcessorValueTreeState& state, juce::File folder, std::vector<FactoryPreset> factory, juce::StringArray excludedIds)
    : state_(state), folder_(std::move(folder)), factory_(std::move(factory)), excluded_(std::move(excludedIds)) {}

juce::String PresetStore::pluginId() const { return state_.state.getType().toString(); }

juce::File PresetStore::fileFor(const juce::String& name) const {
    return folder_.getChildFile(juce::File::createLegalFileName(name.trim()) + ".xml");
}

bool PresetStore::isFactory(const juce::String& name) const {
    return std::any_of(factory_.begin(), factory_.end(), [&](const FactoryPreset& p) { return polish(p.name) == name; });
}

std::optional<PresetStore::Values> PresetStore::readFile(const juce::File& file) const {
    if (!file.existsAsFile() || file.getSize() > kMaxFileBytes) return std::nullopt;
    const auto root = juce::parseXML(file);
    if (root == nullptr || !root->hasTagName(kRootTag) || root->getStringAttribute("plugin") != pluginId()) return std::nullopt;
    Values values;
    for (const auto* child : root->getChildWithTagNameIterator(kParamTag)) {
        const auto value = static_cast<float>(child->getDoubleAttribute("value", std::numeric_limits<double>::quiet_NaN()));
        if (std::isfinite(value)) values[child->getStringAttribute("id")] = value;
    }
    return values;
}

juce::StringArray PresetStore::names() const {
    juce::StringArray all, own;
    for (const auto& preset : factory_) all.add(polish(preset.name));
    for (const auto& file : folder_.findChildFiles(juce::File::findFiles, false, "*.xml")) {
        const auto name = file.getFileNameWithoutExtension();
        if (!all.contains(name) && readFile(file).has_value()) own.add(name);
    }
    own.sortNatural();
    all.addArray(own);
    return all;
}

PresetStore::Values PresetStore::currentValues() const {
    Values values;
    for (auto* p : state_.processor.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (!excluded_.contains(parameter->paramID)) values[parameter->paramID] = parameter->convertFrom0to1(parameter->getValue());
    return values;
}

void PresetStore::apply(const Values& values) {
    for (auto* p : state_.processor.getParameters()) {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (parameter == nullptr || excluded_.contains(parameter->paramID)) continue;
        const auto found = values.find(parameter->paramID);
        const float next = found != values.end() ? parameter->convertTo0to1(found->second) : parameter->getDefaultValue();
        if (std::abs(next - parameter->getValue()) < 1.0e-6f) continue;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(next);
        parameter->endChangeGesture();
    }
}

bool PresetStore::load(const juce::String& name) {
    Values values;
    const auto preset = std::find_if(factory_.begin(), factory_.end(), [&](const FactoryPreset& p) { return polish(p.name) == name; });
    if (preset != factory_.end()) {
        for (const auto& [id, value] : preset->values) values[id] = value;
    } else {
        const auto read = readFile(fileFor(name));
        if (!read.has_value()) return false;
        values = *read;
    }
    apply(values);
    const juce::ScopedLock lock(lock_);
    current_ = name;
    return true;
}

bool PresetStore::save(const juce::String& name, juce::String& error) {
    const auto stem = juce::File::createLegalFileName(name.trim());
    if (stem.trim().isEmpty()) {
        error = polish(text::presetNeedsName);
        return false;
    }
    if (isFactory(stem)) {
        error = polish(text::presetIsFactory);
        return false;
    }
    if (!folder_.createDirectory().wasOk()) {
        error = polish(text::presetNoFolder);
        return false;
    }
    juce::XmlElement root(kRootTag);
    root.setAttribute("plugin", pluginId());
    root.setAttribute("version", kFormatVersion);
    for (const auto& [id, value] : currentValues()) {
        auto* child = root.createNewChildElement(kParamTag);
        child->setAttribute("id", id);
        child->setAttribute("value", static_cast<double>(value));
    }
    const auto file = fileFor(stem);
    if (file.isDirectory() || !file.replaceWithText(root.toString())) {
        error = polish(text::presetNoWrite);
        return false;
    }
    const juce::ScopedLock lock(lock_);
    current_ = stem;
    return true;
}

bool PresetStore::remove(const juce::String& name) {
    if (isFactory(name)) return false;
    const auto file = fileFor(name);
    if (!file.existsAsFile() || !file.deleteFile()) return false;
    const juce::ScopedLock lock(lock_);
    if (current_ == name) current_.clear();
    return true;
}

juce::String PresetStore::currentName() const {
    const juce::ScopedLock lock(lock_);
    return current_;
}

void PresetStore::storeSlot(int index) {
    const auto values = currentValues();
    const juce::ScopedLock lock(lock_);
    slots_[static_cast<size_t>(juce::jlimit(0, 1, index))] = values;
}

void PresetStore::recallSlot(int index) {
    std::optional<Values> values;
    {
        const juce::ScopedLock lock(lock_);
        values = slots_[static_cast<size_t>(juce::jlimit(0, 1, index))];
    }
    if (values.has_value()) apply(*values);
}

void PresetStore::copySlot(int from, int to) {
    const juce::ScopedLock lock(lock_);
    slots_[static_cast<size_t>(juce::jlimit(0, 1, to))] = slots_[static_cast<size_t>(juce::jlimit(0, 1, from))];
}

int PresetStore::activeSlot() const {
    const juce::ScopedLock lock(lock_);
    return activeSlot_;
}

void PresetStore::setActiveSlot(int index) {
    const juce::ScopedLock lock(lock_);
    activeSlot_ = juce::jlimit(0, 1, index);
}

juce::ValueTree PresetStore::toTree() const {
    const juce::ScopedLock lock(lock_);
    juce::ValueTree tree(treeType());
    tree.setProperty("current", current_, nullptr);
    tree.setProperty("active", activeSlot_, nullptr);
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].has_value()) continue;
        juce::ValueTree slot("SLOT");
        slot.setProperty("index", static_cast<int>(i), nullptr);
        for (const auto& [id, value] : *slots_[i]) {
            juce::ValueTree child(kParamTag);
            child.setProperty("id", id, nullptr);
            child.setProperty("value", value, nullptr);
            slot.appendChild(child, nullptr);
        }
        tree.appendChild(slot, nullptr);
    }
    return tree;
}

void PresetStore::fromTree(const juce::ValueTree& tree) {
    const juce::ScopedLock lock(lock_);
    slots_ = {};
    current_ = tree.getProperty("current").toString();
    activeSlot_ = juce::jlimit(0, 1, static_cast<int>(tree.getProperty("active", 0)));
    for (const auto& slot : tree) {
        if (!slot.hasType("SLOT")) continue;
        Values values;
        for (const auto& child : slot) {
            const auto value = static_cast<float>(child.getProperty("value", 0.0f));
            if (std::isfinite(value)) values[child.getProperty("id").toString()] = value;
        }
        slots_[static_cast<size_t>(juce::jlimit(0, 1, static_cast<int>(slot.getProperty("index", 0))))] = std::move(values);
    }
}

} // namespace wers::ui
