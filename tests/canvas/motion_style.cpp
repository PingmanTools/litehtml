#include "test_container.h"
#include "litehtml/motion_style.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace litehtml;

static void require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
static const css_properties& properties(const document::ptr& doc, const char* selector)
{
    const auto element = doc->root()->select_one(selector);
    require(element != nullptr, "test element missing");
    return element->css();
}
static float opacity(const style& value)
{
    const auto& property = value.get_property(_opacity_);
    require(property.is<float>(), "keyframe opacity missing");
    return property.get<float>();
}

int main()
{
    test_container host(300, 300, "");
    const auto doc = document::createFromString(R"HTML(
      <style>
      @keyframes pulse { from { opacity: 1 } to { opacity: .2 } }
      #pulse { animation: pulse 1s ease-in-out; transition: background-color 200ms ease-in-out;
               opacity: 75%; transform: translate(12px,25%) rotate(30deg); }
      #rollback { animation: pulse 2s linear 100ms; animation: broken -1s;
                  animation: second 3s, malformed -2s;
                  transition: opacity 300ms; transition: none, opacity 2s; }
      .priority { animation: pulse 2s !important; }
      #priority { animation-duration: 4s; }
      #lists { animation-name: pulse, other, third; animation-duration: 1s, 2s;
               animation-delay: -250ms; animation-iteration-count: 2.5; }
      @keyframes duplicate { from { opacity: .1 } to { opacity: .9 } }
      @keyframes duplicate { 50% { opacity: .4 } }
      @keyframes merge { from, 50% { opacity: .1; left: 2px; }
                         50% { opacity: .3; } 150% { opacity: .9; }
                         to { opacity: .7 !important; left: 8px; } }
      @keyframes responsive { from { opacity: .1 } }
      @media print { @keyframes responsive { from { opacity: .9 } } }
      @media (min-width: 500px) { @keyframes responsive { from { opacity: .5 } } }
      @keyframes "none" { from { opacity: .4 } }
      @keyframes CaseSensitive { to { opacity: .6 } }
      #quoted { animation: "none" 2s; }
      #disabled { animation-name: none; }
      #parent { opacity: .2; animation: pulse 3s; }
      #inherited { animation: inherit; opacity: inherit; }
      #initial { animation: pulse 3s; animation: initial; }
      </style>
      <div id="pulse"></div><div id="rollback"></div><div id="priority" class="priority"></div>
      <div id="lists"></div><div id="quoted"></div><div id="disabled"></div>
      <div id="parent"><div id="inherited"></div><div id="ordinary"></div></div><div id="initial"></div>
      )HTML", &host);
    require(doc != nullptr, "document creation failed");
    const auto& pulse = properties(doc, "#pulse");
    require(near(pulse.get_opacity(), .75), "computed percentage opacity");
    require(pulse.get_transform().operations.size() == 2, "computed transform list");
    const auto& animation = pulse.get_motion().animations.at(0);
    require(animation.name == "pulse" && !animation.is_none && near(animation.duration, 1), "computed animation shorthand");
    const auto& transition = pulse.get_motion().transitions.at(0);
    require(transition.property == "background-color" && near(transition.duration, .2), "acceptance transition shorthand");
    require(near(transition.timing.sample(.5), .5), "computed transition timing");
    const auto* keyframes = doc->find_keyframes("pulse");
    require(keyframes && keyframes->frames.size() == 2, "acceptance pulse keyframes");
    require(near(opacity(keyframes->frames.at(0)), 1) && near(opacity(keyframes->frames.at(1)), .2), "pulse keyframe values");

    const auto& rollback = properties(doc, "#rollback").get_motion();
    require(rollback.animations.size() == 1 && rollback.animations[0].name == "pulse" &&
            near(rollback.animations[0].duration, 2) && near(rollback.animations[0].delay, .1), "invalid shorthand partial mutation");
    require(rollback.transitions.size() == 1 && rollback.transitions[0].property == "opacity" &&
            near(rollback.transitions[0].duration, .3), "invalid none transition list mutation");
    require(near(properties(doc, "#priority").get_motion().animations[0].duration, 2), "important shorthand cascade");
    const auto& lists = properties(doc, "#lists").get_motion().animations;
    require(lists.size() == 3 && near(lists[0].duration, 1) && near(lists[1].duration, 2) && near(lists[2].duration, 1), "longhand repetition");
    require(near(lists[2].delay, -.25) && near(lists[2].iterations, 2.5), "delay and fractional iterations");

    const auto* duplicate = doc->find_keyframes("duplicate");
    require(duplicate && duplicate->frames.size() == 1 && near(opacity(duplicate->frames.at(.5)), .4), "duplicate keyframes must replace whole rule");
    const auto* merge = doc->find_keyframes("merge");
    require(merge && merge->frames.size() == 3, "invalid keyframe offset retained");
    require(near(opacity(merge->frames.at(.5)), .3), "duplicate offset cascade");
    require(near(merge->frames.at(.5).get_property(_left_).get<css_length>().val(), 2), "duplicate offset lost other declaration");
    require(merge->frames.at(1).get_property(_opacity_).is<invalid>(), "important keyframe declaration accepted");
    require(near(opacity(doc->find_keyframes("responsive")->frames.at(0)), .1), "inactive media keyframes selected");
    host.width = 600;
    doc->media_changed();
    require(near(opacity(doc->find_keyframes("responsive")->frames.at(0)), .5), "media keyframes not updated");

    const auto& quoted = properties(doc, "#quoted").get_motion().animations[0];
    require(quoted.name == "none" && !quoted.is_none && doc->find_keyframes("none"), "quoted none treated as keyword");
    require(properties(doc, "#disabled").get_motion().animations[0].is_none, "unquoted none not disabled");
    require(doc->find_keyframes("CaseSensitive") && !doc->find_keyframes("casesensitive"), "keyframe names lost case");
    require(near(properties(doc, "#inherited").get_opacity(), .2) && near(properties(doc, "#inherited").get_motion().animations[0].duration, 3), "explicit inheritance");
    require(near(properties(doc, "#ordinary").get_opacity(), 1) && properties(doc, "#ordinary").get_motion().animations[0].is_none, "noninherited property leaked");
    require(properties(doc, "#initial").get_motion().animations[0].is_none, "initial shorthand not reset");
    const auto opacity_group = document::createFromString(R"HTML(
      <style>
        #group { opacity: .5; }
        #nested { opacity: .25; }
        #explicit-inherit { opacity: inherit; }
        @keyframes fade-group { from { opacity: 1 } to { opacity: 0 } }
        #animated-group { animation: fade-group 1s linear forwards; }
      </style>
      <div id="group"><div id="ordinary-child"></div>
        <div id="nested"><div id="grandchild"></div></div>
        <div id="explicit-inherit"></div></div>
      <div id="animated-group"><div id="animated-child"></div></div>
      )HTML", &host);
    for (const double time : {0., 500., 1000.})
    {
        opacity_group->set_time(time);
        opacity_group->render(300);
        require(near(properties(opacity_group, "#group").get_opacity(), .5), "static parent opacity changed");
        require(near(properties(opacity_group, "#ordinary-child").get_opacity(), 1), "parent opacity inherited implicitly");
        require(near(properties(opacity_group, "#nested").get_opacity(), .25), "nested opacity multiplied into computed style");
        require(near(properties(opacity_group, "#grandchild").get_opacity(), 1), "nested opacity inherited implicitly");
        require(near(properties(opacity_group, "#explicit-inherit").get_opacity(), .5), "explicit opacity inheritance lost");
        require(near(properties(opacity_group, "#animated-group").get_opacity(), 1 - time / 1000), "animated parent opacity sample");
        require(near(properties(opacity_group, "#animated-child").get_opacity(), 1), "animated opacity leaked into child computed style");
    }
    std::puts("motion style parsing: PASS");
}
