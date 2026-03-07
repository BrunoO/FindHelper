# Gemini API Integration Plan

## Feature Goal

Enable users to describe file search criteria in natural language, and automatically generate Path Pattern matcher syntax (starting with "pp:") using Google AI Gemini API.

## Architecture Overview

### Components

1. **GeminiApiUtils** - Helper functions for API interaction
   - `BuildGeminiPrompt()` - Constructs detailed prompt explaining Path Pattern syntax
   - `CallGeminiApi()` - Makes HTTP request to Gemini API
   - `ParseGeminiResponse()` - Extracts and validates path pattern from API response
   - `GeneratePathPatternFromDescription()` - Main entry point combining all steps

2. **HTTP Client** - Uses Windows WinHTTP API for native HTTP requests
   - Handles authentication (API key)
   - JSON request/response handling
   - Error handling and timeouts

3. **Prompt Engineering** - Detailed prompt explaining:
   - Path Pattern matcher capabilities
   - Syntax rules and examples
   - Output format requirements (must start with "pp:")
   - Common use cases and patterns

## Implementation Details

### API Endpoint

- **URL**: `https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent`
- **Method**: POST
- **Headers**: 
  - `Content-Type: application/json`
  - `x-goog-api-key: <API_KEY>`
- **Request Body**: JSON with prompt text

### Prompt Structure

The prompt will include:
1. **Context**: Explain that we need Path Pattern syntax for file searching
2. **Syntax Documentation**: Complete reference of Path Pattern features
3. **Examples**: Multiple examples showing user descriptions → path patterns
4. **Output Format**: Must return only the pattern starting with "pp:"
5. **User Query**: The actual user description

### Response Parsing

- Parse JSON response from Gemini API
- Extract `candidates[0].content.parts[0].text`
- Validate that response starts with "pp:"
- Strip any extra whitespace or explanation text
- Return clean path pattern string

### Error Handling

- Network errors (timeout, connection failure)
- API errors (invalid key, rate limiting, server errors)
- Invalid response format
- Missing or malformed path pattern in response

## Security Considerations

- API key should be stored securely (not hardcoded)
- Consider environment variable or settings file
- Validate all user input before sending to API
- Sanitize API responses before use

## Testing Strategy

### Unit Tests

1. **Prompt Building**
   - Verify prompt includes all syntax documentation
   - Check examples are included
   - Validate format requirements

2. **Response Parsing**
   - Valid responses with "pp:" prefix
   - Responses with extra text
   - Invalid JSON responses
   - Missing pattern in response

3. **Integration Tests** (with mock HTTP)
   - Successful API calls
   - Network failures
   - API error responses
   - Timeout scenarios

### Test Cases

- Simple descriptions: "all text files" → `pp:**/*.txt`
- Complex descriptions: "C++ source files in src directory" → `pp:src/**/*.cpp`
- Edge cases: empty descriptions, invalid characters
- Error scenarios: network failures, API errors

## Future Enhancements

- Caching of common queries
- Support for multiple AI models
- User feedback mechanism to improve prompts
- Batch processing for multiple descriptions
- Local fallback patterns for common queries

## Dependencies

- Windows WinHTTP API (native)
- nlohmann/json (already in project)
- PathPatternMatcher (for validation)

## Files to Create

1. `GeminiApiUtils.h` - Header with function declarations
2. `GeminiApiUtils.cpp` - Implementation
3. `tests/GeminiApiUtilsTests.cpp` - Unit tests

## Integration Points

- **Settings**: Store API key in settings.json
- **UI**: Add button/text field for natural language input
- **SearchController**: Use generated pattern for search




