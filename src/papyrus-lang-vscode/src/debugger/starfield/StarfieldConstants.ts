export class StarfieldConstants {
    public static readonly NATIVE_FORMS = [
        'Action',
        'Activator',
        'ActiveMagicEffect',
        'Actor',
        'ActorBase',
        'ActorValue',
        'AffinityEvent',
        'Alias',
        'Ammo',
        'Armor',
        'AssociationType',
        'Book',
        'CameraShot',
        'Cell',
        'Challenge',
        'Class',
        'CombatStyle',
        'ConditionForm',
        'ConstructibleObject',
        'Container',
        'Curve',
        'Debug',
        'Door',
        'EffectShader',
        'Enchantment',
        'Explosion',
        'Faction',
        'Flora',
        'Form',
        'FormList',
        'Furniture',
        'GlobalVariable',
        'Hazard',
        'HeadPart',
        'Idle',
        'IdleMarker',
        'ImageSpaceModifier',
        'ImpactDataSet',
        'Ingredient',
        'InputEnableLayer',
        'InstanceNamingRules',
        'Key',
        'Keyword',
        'LegendaryItem',
        'LeveledActor',
        'LeveledItem',
        'LeveledSpaceshipBase',
        'LeveledSpell',
        'Light',
        'Location',
        'LocationAlias',
        'LocationRefType',
        'MagicEffect',
        'Message',
        'MiscObject',
        'MovableStatic',
        'MusicType',
        'Note',
        'ObjectMod',
        'ObjectReference',
        'Outfit',
        'Package',
        'PackIn',
        'Perk',
        'Planet',
        'Potion',
        'Projectile',
        'Quest',
        'Race',
        'RefCollectionAlias',
        'ReferenceAlias',
        'ResearchProject',
        'Resource',
        'Scene',
        'Scroll',
        'ShaderParticleGeometry',
        'Shout',
        'SoulGem',
        'SpaceshipBase',
        'SpaceshipReference',
        'Spell',
        'Static',
        'TalkingActivator',
        'Terminal',
        'TerminalMenu',
        'TextureSet',
        'Topic',
        'TopicInfo',
        'VisualEffect',
        'VoiceType',
        'Weapon',
        'Weather',
        'WordOfPower',
        'WorldSpace',
        'WwiseEvent',
        // Non Form Natives
        'SpeechChallengeObject', // not technically a form according to the game? but still a form according to papyrus...
        'Game',
        'Math',
        'ScriptObject', // This is the form that every Papyrus object inherits from
        'Utility',
    ];

    public static readonly checkNativeForm = (form: string): boolean => {
        return StarfieldConstants.NATIVE_FORMS.includes(form);
    };
}
