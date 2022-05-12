## Environment Configs

This folder contains environment-specific overrides for iam metadata.

Commands from group `ycp iam inner metadata` will pick up a file either explicitly specified (--env-file flag) or auto discovered from a ycp profile.

Currently, there are two kinds of overrides supported in env files - role visibility and OAuth client state.

### Role visibility

Public roles are created as internal and then go public gradually from the testing stand to production.

### OAuth client state

It's now possible to set `lifecycle_state: deleted` for a client, if it doesn't need to exist in a particular environment.

Clients have also different settings on each environment like urls and secrets. This is not covered by the current API version and will be implemented in CLOUD-93508.

