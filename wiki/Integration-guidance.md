# Integration guidance

This guidance records the intent found in the original ODPAPI specification
and applies standard operational precautions. It does not define a complete
security profile or replace a provider's current integration documentation.

- Use HTTPS for deployed integrations.
- Authenticate every request using the agreed HTTP authentication mechanism.
- Keep credentials in a secure runtime configuration. Never commit test or
  production credentials or tokens.
- Agree with the provider on library GUID scope and mapping; the protocol does
  not make provider-encoded GUIDs globally meaningful.
- Preserve request sequence numbers when correlating per-object results.
- Handle the common result separately from each object's operation status.
- Use a provider-controlled test endpoint and test account for integration
  checks. Do not load captured production responses as public sample data.

The legacy co-development agreement in `documents/license/` describes
provider-specific test access and usage terms. It is a historical agreement,
not a protocol license or general ODPAPI requirement; parties must review any
current agreement directly with the relevant provider.
