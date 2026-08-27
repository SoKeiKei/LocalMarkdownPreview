#define ProductName "Local Markdown Preview"
#define ProductVersion "0.1.0-dev"
#define PublisherName "LocalMarkdownPreview contributors"
#define InstallerAppId "{{C846D84E-793E-4FD5-9B65-FBF6AED37799}"
#define HandlerClsid "{{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}"
#define HandlerAppId "{{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}"
#define HandlerProgId "LocalMarkdownPreview.Handler.1"
#define HandlerVersionIndependentProgId "LocalMarkdownPreview.Handler"
#define PreviewHandlerCategory "{{8895b1c6-b41f-4c1c-a562-0d564250836f}"

[Setup]
AppId={#InstallerAppId}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppPublisher={#PublisherName}
DefaultDirName={autopf}\Local Markdown Preview
DefaultGroupName={#ProductName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0.19045
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=LocalMarkdownPreview-0.1.0-dev-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes
UninstallDisplayName={#ProductName}
VersionInfoDescription={#ProductName} installer
VersionInfoProductName={#ProductName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\Release\LocalMarkdownPreview.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\PreviewHostCleanup.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\DEPENDENCIES.lock.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\sbom.spdx.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\architecture.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\build-and-registration.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\compatibility-matrix.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\performance-report.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\installation-test-report.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\known-issues.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\threat-model.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\third_party\md4c\LICENSE.md"; DestDir: "{app}\licenses"; DestName: "MD4C-LICENSE.md"; Flags: ignoreversion
Source: "..\third_party\md4c\PATCHES.md"; DestDir: "{app}\licenses"; DestName: "MD4C-PATCHES.md"; Flags: ignoreversion

[Registry]
; The COM class and dedicated Prevhost AppID are always machine-wide.
Root: HKLM64; Subkey: "Software\Classes\CLSID\{#HandlerClsid}"; ValueType: string; ValueName: ""; ValueData: "Local Markdown Preview Handler"
Root: HKLM64; Subkey: "Software\Classes\CLSID\{#HandlerClsid}"; ValueType: string; ValueName: "AppID"; ValueData: "{#HandlerAppId}"
Root: HKLM64; Subkey: "Software\Classes\CLSID\{#HandlerClsid}"; ValueType: string; ValueName: "ProgID"; ValueData: "{#HandlerProgId}"
Root: HKLM64; Subkey: "Software\Classes\CLSID\{#HandlerClsid}"; ValueType: string; ValueName: "VersionIndependentProgID"; ValueData: "{#HandlerVersionIndependentProgId}"
Root: HKLM64; Subkey: "Software\Classes\CLSID\{#HandlerClsid}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{app}\LocalMarkdownPreview.dll"
Root: HKLM64; Subkey: "Software\Classes\CLSID\{#HandlerClsid}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"
Root: HKLM64; Subkey: "Software\Classes\AppID\{#HandlerAppId}"; ValueType: string; ValueName: ""; ValueData: "Local Markdown Preview Handler"
Root: HKLM64; Subkey: "Software\Classes\AppID\{#HandlerAppId}"; ValueType: string; ValueName: "DllSurrogate"; ValueData: "Prevhost.exe"
Root: HKLM64; Subkey: "Software\Microsoft\Windows\CurrentVersion\PreviewHandlers"; ValueType: string; ValueName: "{#HandlerClsid}"; ValueData: "Local Markdown Preview Handler"
Root: HKLM64; Subkey: "Software\Classes\{#HandlerProgId}"; ValueType: string; ValueName: ""; ValueData: "Local Markdown Preview Handler"
Root: HKLM64; Subkey: "Software\Classes\{#HandlerProgId}\shellex\{#PreviewHandlerCategory}"; ValueType: string; ValueName: ""; ValueData: "{#HandlerClsid}"
Root: HKLM64; Subkey: "Software\Classes\{#HandlerVersionIndependentProgId}"; ValueType: string; ValueName: ""; ValueData: "Local Markdown Preview Handler"
Root: HKLM64; Subkey: "Software\Classes\{#HandlerVersionIndependentProgId}\shellex\{#PreviewHandlerCategory}"; ValueType: string; ValueName: ""; ValueData: "{#HandlerClsid}"
Root: HKLM64; Subkey: "Software\Classes\.md\shellex\{#PreviewHandlerCategory}"; ValueType: string; ValueName: ""; ValueData: "{#HandlerClsid}"; Check: ShouldWriteProjectMappings
Root: HKLM64; Subkey: "Software\Classes\.markdown\shellex\{#PreviewHandlerCategory}"; ValueType: string; ValueName: ""; ValueData: "{#HandlerClsid}"; Check: ShouldWriteProjectMappings
Root: HKCU; Subkey: "Software\Classes\.md\shellex\{#PreviewHandlerCategory}"; ValueType: string; ValueName: ""; ValueData: "{#HandlerClsid}"; Check: ShouldWriteProjectMappings
Root: HKCU; Subkey: "Software\Classes\.markdown\shellex\{#PreviewHandlerCategory}"; ValueType: string; ValueName: ""; ValueData: "{#HandlerClsid}"; Check: ShouldWriteProjectMappings

[UninstallDelete]
Type: files; Name: "{app}\registration-backup.json"
Type: dirifempty; Name: "{app}\licenses"
Type: dirifempty; Name: "{app}\docs"
Type: dirifempty; Name: "{app}"

[UninstallRun]
Filename: "{app}\PreviewHostCleanup.exe"; Parameters: """{app}\LocalMarkdownPreview.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "StopDedicatedPreviewHost"

[Code]
const
  HandlerClsid = '{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}';
  HandlerAppId = '{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}';
  HandlerProgId = 'LocalMarkdownPreview.Handler.1';
  HandlerVersionIndependentProgId = 'LocalMarkdownPreview.Handler';
  PreviewHandlerCategory = '{8895b1c6-b41f-4c1c-a562-0d564250836f}';
  ProductDllName = 'LocalMarkdownPreview.dll';
  BackupFileName = 'registration-backup.json';
  ChoiceReplace = 0;
  ChoiceKeep = 1;
  ChoiceCancel = 2;

procedure SHChangeNotify(EventID, Flags: Cardinal; Item1, Item2: Integer);
  external 'SHChangeNotify@shell32.dll stdcall';

var
  ConflictPage: TInputOptionWizardPage;
  ConflictsFound: Boolean;
  ConflictChoice: Integer;
  ConflictReport: String;
  BackupReady: Boolean;
  RefreshSent: Boolean;

function TextContains(const Text, Needle: String): Boolean;
begin
  Result := Pos(Uppercase(Needle), Uppercase(Text)) > 0;
end;

procedure AppendLine(var Text: String; const Line: String);
begin
  if Text <> '' then
    Text := Text + #13#10;
  Text := Text + Line;
end;

function ScopeName(const RootKeyName: String): String;
begin
  Result := RootKeyName;
end;

function BoolText(const Value: Boolean): String;
begin
  if Value then
    Result := 'True'
  else
    Result := 'False';
end;

function RegRootForScope(const Scope: String): Integer;
begin
  if Uppercase(Scope) = 'HKCU' then
    Result := HKEY_CURRENT_USER
  else
    Result := HKEY_LOCAL_MACHINE;
end;

function ReadStringValue(const RootKey: Integer; const SubKeyName, ValueName: String; var Value: String): Boolean;
begin
  Value := '';
  Result := RegQueryStringValue(RootKey, SubKeyName, ValueName, Value);
end;

function ReadDwordValue(const RootKey: Integer; const SubKeyName, ValueName: String; var Value: Cardinal): Boolean;
begin
  Value := 0;
  Result := RegQueryDWordValue(RootKey, SubKeyName, ValueName, Value);
end;

function ValueTypeName(const RootKey: Integer; const SubKeyName, ValueName: String; var Value: String): String;
var
  DwordValue: Cardinal;
begin
  Value := '';
  if not RegValueExists(RootKey, SubKeyName, ValueName) then begin
    Result := 'NONE';
    Exit;
  end;
  if ReadStringValue(RootKey, SubKeyName, ValueName, Value) then begin
    { Inno's registry API exposes string data uniformly. Keep the distinction
      explicit in the backup, and retain the raw value for safe restoration. }
    if TextContains(Value, '%SYSTEMROOT%') then
      Result := 'REG_EXPAND_SZ'
    else
      Result := 'REG_SZ';
    Exit;
  end;
  if ReadDwordValue(RootKey, SubKeyName, ValueName, DwordValue) then begin
    Value := IntToStr(DwordValue);
    Result := 'REG_DWORD';
    Exit;
  end;
  Result := 'UNSUPPORTED';
end;

function JsonEscape(const Value: String): String;
begin
  Result := Value;
  StringChangeEx(Result, '\', '\\', True);
  StringChangeEx(Result, '"', '\"', True);
  StringChangeEx(Result, #13, '\r', True);
  StringChangeEx(Result, #10, '\n', True);
  StringChangeEx(Result, #9, '\t', True);
end;

function JsonUnescape(const Value: String): String;
var
  I: Integer;
  Character: Char;
begin
  Result := '';
  I := 1;
  while I <= Length(Value) do begin
    Character := Value[I];
    if (Character = '\') and (I < Length(Value)) then begin
      I := I + 1;
      Character := Value[I];
      if Character = 'r' then
        Result := Result + #13
      else if Character = 'n' then
        Result := Result + #10
      else if Character = 't' then
        Result := Result + #9
      else if Character = '"' then
        Result := Result + '"'
      else if Character = '\' then
        Result := Result + '\'
      else
        Result := Result + '\' + Character;
    end
    else
      Result := Result + Character;
    I := I + 1;
  end;
end;

function JsonField(const Line, FieldName: String; var Found: Boolean): String;
var
  Needle: String;
  StartAt: Integer;
  Cursor: Integer;
  Escaped: Boolean;
  RawValue: String;
begin
  Result := '';
  Found := False;
  Needle := '"' + FieldName + '":"';
  StartAt := Pos(Needle, Line);
  if StartAt = 0 then
    Exit;
  StartAt := StartAt + Length(Needle);
  Cursor := StartAt;
  Escaped := False;
  while Cursor <= Length(Line) do begin
    if (Line[Cursor] = '"') and (not Escaped) then begin
      RawValue := Copy(Line, StartAt, Cursor - StartAt);
      Result := JsonUnescape(RawValue);
      Found := True;
      Exit;
    end;
    if (Line[Cursor] = '\') and (not Escaped) then
      Escaped := True
    else
      Escaped := False;
    Cursor := Cursor + 1;
  end;
end;

function BackupPath: String;
begin
  Result := AddBackslash(ExpandConstant('{app}')) + BackupFileName;
end;

procedure AddBackupEntry(var Json: String; const EntryId, Scope, SubKeyName, ValueName, ExpectedValue: String);
var
  RootKey: Integer;
  ExistingValue: String;
  TypeName: String;
  Exists: Boolean;
  KeyExists: Boolean;
  Entry: String;
begin
  RootKey := RegRootForScope(Scope);
  KeyExists := RegKeyExists(RootKey, SubKeyName);
  Exists := RegValueExists(RootKey, SubKeyName, ValueName);
  TypeName := ValueTypeName(RootKey, SubKeyName, ValueName, ExistingValue);
  Entry := '    {"id":"' + JsonEscape(EntryId) +
    '","scope":"' + JsonEscape(Scope) +
    '","subKey":"' + JsonEscape(SubKeyName) +
    '","valueName":"' + JsonEscape(ValueName) +
    '","keyExists":"' + BoolText(KeyExists) +
    '","exists":"' + BoolText(Exists) +
    '","type":"' + JsonEscape(TypeName) +
    '","value":"' + JsonEscape(ExistingValue) +
    '","expected":"' + JsonEscape(ExpectedValue) + '"}';
  if Json <> '' then
    Json := Json + ',' + #13#10;
  Json := Json + Entry;
end;

function ReadBackupEntry(const Content, EntryId: String; var Scope, SubKeyName, ValueName, ExistsText, TypeName, Value, Expected: String): Boolean;
var
  SearchAt: Integer;
  LineEnd: Integer;
  Line: String;
  Found: Boolean;
  IdText: String;
begin
  Result := False;
  SearchAt := 1;
  while SearchAt <= Length(Content) do begin
    LineEnd := Pos(#10, Copy(Content, SearchAt, Length(Content) - SearchAt + 1));
    if LineEnd = 0 then
      Line := Copy(Content, SearchAt, Length(Content) - SearchAt + 1)
    else
      Line := Copy(Content, SearchAt, LineEnd - 1);
    IdText := JsonField(Line, 'id', Found);
    if Found and (IdText = EntryId) then begin
      Scope := JsonField(Line, 'scope', Found);
      if not Found then Exit;
      SubKeyName := JsonField(Line, 'subKey', Found);
      if not Found then Exit;
      ValueName := JsonField(Line, 'valueName', Found);
      if not Found then Exit;
      ExistsText := JsonField(Line, 'exists', Found);
      if not Found then Exit;
      TypeName := JsonField(Line, 'type', Found);
      if not Found then Exit;
      Value := JsonField(Line, 'value', Found);
      if not Found then Exit;
      Expected := JsonField(Line, 'expected', Found);
      if not Found then Exit;
      Result := True;
      Exit;
    end;
    if LineEnd = 0 then
      Break;
    SearchAt := SearchAt + LineEnd;
  end;
end;

function MappingSubKey(const Extension: String): String;
begin
  Result := 'Software\Classes\' + Extension + '\shellex\' + PreviewHandlerCategory;
end;

function IsExpectedMapping(const RootKey: Integer; const Extension: String): Boolean;
var
  CurrentValue: String;
begin
  Result := ReadStringValue(RootKey, MappingSubKey(Extension), '', CurrentValue) and
    (CompareText(CurrentValue, HandlerClsid) = 0);
end;

function IsPowerToysName(const Name, Value: String): Boolean;
begin
  Result := TextContains(Name, 'PowerToys') or TextContains(Value, 'PowerToys');
end;

procedure InspectPreviewHandlerList(const RootKey: Integer; const Scope: String; var FoundExternal: Boolean);
var
  Names: TArrayOfString;
  I: Integer;
  HandlerValue: String;
  Line: String;
begin
  if not RegGetValueNames(RootKey, 'Software\Microsoft\Windows\CurrentVersion\PreviewHandlers', Names) then
    Exit;
  for I := 0 to GetArrayLength(Names) - 1 do begin
    HandlerValue := '';
    RegQueryStringValue(RootKey, 'Software\Microsoft\Windows\CurrentVersion\PreviewHandlers', Names[I], HandlerValue);
    Line := Scope + ' PreviewHandlers: ' + Names[I] + ' = ' + HandlerValue;
    Log(Line);
    if (CompareText(Names[I], HandlerClsid) <> 0) and
      (TextContains(Names[I], 'Markdown') or TextContains(HandlerValue, 'Markdown') or
       IsPowerToysName(Names[I], HandlerValue)) then begin
      FoundExternal := True;
      AppendLine(ConflictReport, Line);
    end;
  end;
end;

procedure InspectMapping(const RootKey: Integer; const Scope, Extension: String; var FoundExternal: Boolean);
var
  CurrentValue: String;
  DisplayName: String;
  Line: String;
begin
  if not RegValueExists(RootKey, MappingSubKey(Extension), '') then
    Exit;
  if not ReadStringValue(RootKey, MappingSubKey(Extension), '', CurrentValue) then
    CurrentValue := '<non-string registry value>';
  if CompareText(CurrentValue, HandlerClsid) = 0 then begin
    Line := Scope + ' ' + Extension + ' mapping: this project CLSID';
    Log(Line);
    Exit;
  end;
  DisplayName := '';
  if (CurrentValue <> '<non-string registry value>') then begin
    ReadStringValue(RootKey, 'Software\Classes\CLSID\' + CurrentValue, '', DisplayName);
    if DisplayName = '' then
      ReadStringValue(HKEY_CURRENT_USER, 'Software\Classes\CLSID\' + CurrentValue, '', DisplayName);
  end;
  if DisplayName = '' then
    DisplayName := '<name unavailable>';
  Line := Scope + ' ' + Extension + ' mapping: ' + CurrentValue + ' (' + DisplayName + ')';
  Log(Line);
  FoundExternal := True;
  AppendLine(ConflictReport, Line);
end;

procedure DetectConflicts;
var
  ExternalFound: Boolean;
  ExistingClassPath: String;
  ExistingAppIdSurrogate: String;
  Line: String;
begin
  ConflictReport := '';
  ExternalFound := False;
  InspectMapping(HKEY_CURRENT_USER, 'HKCU', '.md', ExternalFound);
  InspectMapping(HKEY_CURRENT_USER, 'HKCU', '.markdown', ExternalFound);
  InspectMapping(HKEY_LOCAL_MACHINE, 'HKLM64', '.md', ExternalFound);
  InspectMapping(HKEY_LOCAL_MACHINE, 'HKLM64', '.markdown', ExternalFound);
  InspectPreviewHandlerList(HKEY_CURRENT_USER, 'HKCU', ExternalFound);
  InspectPreviewHandlerList(HKEY_LOCAL_MACHINE, 'HKLM64', ExternalFound);
  ExistingClassPath := '';
  if ReadStringValue(HKEY_LOCAL_MACHINE,
      'Software\Classes\CLSID\' + HandlerClsid + '\InprocServer32', '', ExistingClassPath) and
      (CompareText(ExistingClassPath,
        AddBackslash(ExpandConstant('{autopf}\Local Markdown Preview')) + ProductDllName) <> 0) then begin
    Line := 'HKLM64 project CLSID is already owned by another path: ' + ExistingClassPath;
    Log(Line);
    ExternalFound := True;
    AppendLine(ConflictReport, Line);
  end;
  ExistingAppIdSurrogate := '';
  if ReadStringValue(HKEY_LOCAL_MACHINE,
      'Software\Classes\AppID\' + HandlerAppId, 'DllSurrogate', ExistingAppIdSurrogate) and
      (CompareText(ExistingAppIdSurrogate, 'Prevhost.exe') <> 0) and
      (CompareText(ExistingAppIdSurrogate, '%SystemRoot%\system32\prevhost.exe') <> 0) and
      (CompareText(ExistingAppIdSurrogate, ExpandConstant('{sys}\prevhost.exe')) <> 0) then begin
    Line := 'HKLM64 project AppID is already owned by another surrogate: ' + ExistingAppIdSurrogate;
    Log(Line);
    ExternalFound := True;
    AppendLine(ConflictReport, Line);
  end;
  ConflictsFound := ExternalFound;
  if not ConflictsFound then
    Log('No external Markdown preview handler mapping was found.');
end;

function ShouldWriteProjectMappings: Boolean;
begin
  Result := (not ConflictsFound) or (ConflictChoice = ChoiceReplace);
end;

procedure SaveInitialBackup;
var
  Json: String;
  Lines: TArrayOfString;
  Path: String;
begin
  Path := BackupPath;
  ForceDirectories(ExpandConstant('{app}'));
  Json := '{' + #13#10 +
    '  "schemaVersion": 1,' + #13#10 +
    '  "product": "Local Markdown Preview",' + #13#10 +
    '  "entries": [' + #13#10;
  AddBackupEntry(Json, 'hkcu-md-mapping', 'HKCU', MappingSubKey('.md'), '', HandlerClsid);
  AddBackupEntry(Json, 'hkcu-markdown-mapping', 'HKCU', MappingSubKey('.markdown'), '', HandlerClsid);
  AddBackupEntry(Json, 'hklm64-md-mapping', 'HKLM64', MappingSubKey('.md'), '', HandlerClsid);
  AddBackupEntry(Json, 'hklm64-markdown-mapping', 'HKLM64', MappingSubKey('.markdown'), '', HandlerClsid);
  AddBackupEntry(Json, 'class-default', 'HKLM64', 'Software\Classes\CLSID\' + HandlerClsid, '', 'Local Markdown Preview Handler');
  AddBackupEntry(Json, 'class-appid', 'HKLM64', 'Software\Classes\CLSID\' + HandlerClsid, 'AppID', HandlerAppId);
  AddBackupEntry(Json, 'class-progid', 'HKLM64', 'Software\Classes\CLSID\' + HandlerClsid, 'ProgID', HandlerProgId);
  AddBackupEntry(Json, 'class-version-independent-progid', 'HKLM64', 'Software\Classes\CLSID\' + HandlerClsid, 'VersionIndependentProgID', HandlerVersionIndependentProgId);
  AddBackupEntry(Json, 'class-inproc-default', 'HKLM64', 'Software\Classes\CLSID\' + HandlerClsid + '\InprocServer32', '', ExpandConstant('{app}\') + ProductDllName);
  AddBackupEntry(Json, 'class-inproc-threading', 'HKLM64', 'Software\Classes\CLSID\' + HandlerClsid + '\InprocServer32', 'ThreadingModel', 'Apartment');
  AddBackupEntry(Json, 'appid-default', 'HKLM64', 'Software\Classes\AppID\' + HandlerAppId, '', 'Local Markdown Preview Handler');
  AddBackupEntry(Json, 'appid-surrogate', 'HKLM64', 'Software\Classes\AppID\' + HandlerAppId, 'DllSurrogate', 'Prevhost.exe');
  AddBackupEntry(Json, 'preview-handler-list', 'HKLM64', 'Software\Microsoft\Windows\CurrentVersion\PreviewHandlers', HandlerClsid, 'Local Markdown Preview Handler');
  AddBackupEntry(Json, 'progid-default', 'HKLM64', 'Software\Classes\' + HandlerProgId, '', 'Local Markdown Preview Handler');
  AddBackupEntry(Json, 'progid-mapping', 'HKLM64', 'Software\Classes\' + HandlerProgId + '\shellex\' + PreviewHandlerCategory, '', HandlerClsid);
  AddBackupEntry(Json, 'version-independent-progid-default', 'HKLM64', 'Software\Classes\' + HandlerVersionIndependentProgId, '', 'Local Markdown Preview Handler');
  AddBackupEntry(Json, 'version-independent-progid-mapping', 'HKLM64', 'Software\Classes\' + HandlerVersionIndependentProgId + '\shellex\' + PreviewHandlerCategory, '', HandlerClsid);
  Json := Json + #13#10 + '  ]' + #13#10 + '}' + #13#10;
  SetArrayLength(Lines, 1);
  Lines[0] := Json;
  if not SaveStringsToUTF8FileWithoutBOM(Path, Lines, False) then
    RaiseException('Could not save the registration backup: ' + Path);
  BackupReady := True;
end;

procedure StopExistingDedicatedHost;
var
  HelperPath: String;
  ExitCode: Integer;
begin
  HelperPath := ExpandConstant('{app}\PreviewHostCleanup.exe');
  if not FileExists(HelperPath) then
    Exit;
  if not Exec(HelperPath,
      '"' + ExpandConstant('{app}\LocalMarkdownPreview.dll') + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Log('Could not start the existing dedicated host cleanup helper.')
  else if ExitCode <> 0 then
    Log('The existing dedicated host cleanup helper returned ' + IntToStr(ExitCode) + '.');
end;

function LoadBackupText(var Content: String): Boolean;
var
  Lines: TArrayOfString;
  I: Integer;
begin
  Content := '';
  Result := False;
  if not LoadStringsFromFile(BackupPath, Lines) then
    Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do begin
    if I > 0 then
      Content := Content + #13#10;
    Content := Content + Lines[I];
  end;
  Result := True;
end;

function BackupExistsAndLooksValid: Boolean;
var
  Content: String;
begin
  Result := False;
  if not FileExists(BackupPath) then
    Exit;
  if not LoadBackupText(Content) then
    Exit;
  Result := Pos('"schemaVersion": 1', Content) > 0;
end;

function CurrentClassBelongsToProduct: Boolean;
var
  CurrentPath: String;
  ExpectedPath: String;
begin
  ExpectedPath := ExpandConstant('{app}\') + ProductDllName;
  Result := ReadStringValue(HKEY_LOCAL_MACHINE,
    'Software\Classes\CLSID\' + HandlerClsid + '\InprocServer32', '', CurrentPath) and
    (CompareText(CurrentPath, ExpectedPath) = 0);
end;

function CurrentSurrogateBelongsToProduct: Boolean;
var
  CurrentValue: String;
begin
  Result := ReadStringValue(HKEY_LOCAL_MACHINE, 'Software\Classes\AppID\' + HandlerAppId, 'DllSurrogate', CurrentValue) and
    ((CompareText(CurrentValue, 'Prevhost.exe') = 0) or
     (CompareText(CurrentValue, '%SystemRoot%\system32\prevhost.exe') = 0) or
     (CompareText(CurrentValue, ExpandConstant('{sys}\prevhost.exe')) = 0));
end;

function RestoreOrDeleteEntry(const Content, EntryId: String): Boolean;
var
  Scope: String;
  SubKeyName: String;
  ValueName: String;
  ExistsText: String;
  TypeName: String;
  OriginalValue: String;
  ExpectedValue: String;
  RootKey: Integer;
  CurrentValue: String;
  DwordValue: Cardinal;
begin
  Result := False;
  if not ReadBackupEntry(Content, EntryId, Scope, SubKeyName, ValueName, ExistsText, TypeName, OriginalValue, ExpectedValue) then begin
    Log('Backup entry missing: ' + EntryId);
    Exit;
  end;
  RootKey := RegRootForScope(Scope);
  if not ReadStringValue(RootKey, SubKeyName, ValueName, CurrentValue) then begin
    if RegValueExists(RootKey, SubKeyName, ValueName) then begin
      Log('Keeping non-string replacement for ' + EntryId);
      Exit;
    end;
    if (CompareText(ExistsText, 'True') = 0) then begin
      Log('Keeping missing current value for ' + EntryId + ' because it changed after install.');
      Exit;
    end;
  end
  else if (ExpectedValue <> '') and (CompareText(CurrentValue, ExpectedValue) <> 0) then begin
    Log('Keeping user replacement for ' + EntryId + ': ' + CurrentValue);
    Exit;
  end;
  if CompareText(ExistsText, 'True') = 0 then begin
    if CompareText(TypeName, 'REG_DWORD') = 0 then begin
      DwordValue := StrToIntDef(OriginalValue, 0);
      Result := RegWriteDWordValue(RootKey, SubKeyName, ValueName, DwordValue);
    end
    else if (CompareText(TypeName, 'REG_SZ') = 0) or (CompareText(TypeName, 'REG_EXPAND_SZ') = 0) then begin
      if CompareText(TypeName, 'REG_EXPAND_SZ') = 0 then
        Result := RegWriteExpandStringValue(RootKey, SubKeyName, ValueName, OriginalValue)
      else
        Result := RegWriteStringValue(RootKey, SubKeyName, ValueName, OriginalValue);
    end
    else begin
      Log('Unsupported original registry type for ' + EntryId + '; value was left unchanged.');
      Exit;
    end;
  end
  else begin
    Result := RegDeleteValue(RootKey, SubKeyName, ValueName);
  end;
end;

procedure DeleteEmptyProductKeys;
begin
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\CLSID\' + HandlerClsid + '\InprocServer32');
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\CLSID\' + HandlerClsid);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\AppID\' + HandlerAppId);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\' + HandlerProgId + '\shellex\' + PreviewHandlerCategory);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\' + HandlerProgId + '\shellex');
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\' + HandlerProgId);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\' + HandlerVersionIndependentProgId + '\shellex\' + PreviewHandlerCategory);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\' + HandlerVersionIndependentProgId + '\shellex');
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\' + HandlerVersionIndependentProgId);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Microsoft\Windows\CurrentVersion\PreviewHandlers');
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\.md\shellex\' + PreviewHandlerCategory);
  RegDeleteKeyIfEmpty(HKEY_LOCAL_MACHINE, 'Software\Classes\.markdown\shellex\' + PreviewHandlerCategory);
  RegDeleteKeyIfEmpty(HKEY_CURRENT_USER, 'Software\Classes\.md\shellex\' + PreviewHandlerCategory);
  RegDeleteKeyIfEmpty(HKEY_CURRENT_USER, 'Software\Classes\.markdown\shellex\' + PreviewHandlerCategory);
end;

procedure RestoreRegistrations;
var
  Content: String;
  ClassOwned: Boolean;
  SurrogateOwned: Boolean;
begin
  if not LoadBackupText(Content) then begin
    Log('Registration backup could not be read; no original values were changed.');
    Exit;
  end;
  RestoreOrDeleteEntry(Content, 'hkcu-md-mapping');
  RestoreOrDeleteEntry(Content, 'hkcu-markdown-mapping');
  RestoreOrDeleteEntry(Content, 'hklm64-md-mapping');
  RestoreOrDeleteEntry(Content, 'hklm64-markdown-mapping');
  ClassOwned := CurrentClassBelongsToProduct;
  SurrogateOwned := CurrentSurrogateBelongsToProduct;
  if ClassOwned then begin
    RestoreOrDeleteEntry(Content, 'class-default');
    RestoreOrDeleteEntry(Content, 'class-appid');
    RestoreOrDeleteEntry(Content, 'class-progid');
    RestoreOrDeleteEntry(Content, 'class-version-independent-progid');
    RestoreOrDeleteEntry(Content, 'class-inproc-default');
    RestoreOrDeleteEntry(Content, 'class-inproc-threading');
    RestoreOrDeleteEntry(Content, 'appid-default');
    RestoreOrDeleteEntry(Content, 'appid-surrogate');
    RestoreOrDeleteEntry(Content, 'preview-handler-list');
    RestoreOrDeleteEntry(Content, 'progid-default');
    RestoreOrDeleteEntry(Content, 'progid-mapping');
    RestoreOrDeleteEntry(Content, 'version-independent-progid-default');
    RestoreOrDeleteEntry(Content, 'version-independent-progid-mapping');
  end
  else
    Log('COM class no longer points to this installation path; project class values were preserved.');
  if SurrogateOwned and (not ClassOwned) then begin
    { A separately changed CLSID must not cause us to remove a shared AppID. }
    Log('AppID surrogate was retained because the COM class ownership check failed.');
  end;
  if ClassOwned then
    DeleteEmptyProductKeys;
end;

procedure SendShellRefresh;
begin
  if RefreshSent then
    Exit;
  SHChangeNotify($08000000, 0, 0, 0);
  RefreshSent := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  HadConflicts: Boolean;
begin
  Result := '';
  NeedsRestart := False;
  DetectConflicts;
  HadConflicts := ConflictsFound;
  if HadConflicts and (ConflictChoice = ChoiceCancel) then begin
    Result := 'An existing Markdown preview handler was found. Select Replace or Keep existing handler before installing.';
    Exit;
  end;
  if (not HadConflicts) and (ConflictChoice = ChoiceKeep) then
    ConflictChoice := ChoiceReplace;
  StopExistingDedicatedHost;
  if BackupReady then
    Exit;
  if FileExists(BackupPath) then begin
    if not BackupExistsAndLooksValid then begin
      Result := 'The existing registration backup is not valid JSON; installation was stopped safely.';
      Exit;
    end;
    BackupReady := True;
    Exit;
  end;
  if CurrentClassBelongsToProduct then begin
    Result := 'The product registration exists but its original backup is missing; repair was stopped to avoid overwriting it.';
    Exit;
  end;
  SaveInitialBackup;
end;

procedure InitializeWizard;
begin
  BackupReady := False;
  RefreshSent := False;
  ConflictChoice := ChoiceCancel;
  DetectConflicts;
  if not ConflictsFound then
    ConflictChoice := ChoiceReplace
  else begin
    ConflictPage := CreateInputOptionPage(wpSelectDir,
      'Existing preview handler detected',
      'Choose how to handle the existing Markdown preview handler',
      'The installer found the following entries. Replacement is never silent.' + #13#10 + #13#10 + ConflictReport,
      True, True);
    ConflictPage.Add('Replace the existing .md/.markdown preview mappings');
    ConflictPage.Add('Keep the existing handler, but continue installing this product');
    ConflictPage.Add('Cancel installation');
    ConflictPage.SelectedValueIndex := ChoiceCancel;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := Assigned(ConflictPage) and (PageID = ConflictPage.ID) and (not ConflictsFound);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if Assigned(ConflictPage) and (CurPageID = ConflictPage.ID) then begin
    ConflictChoice := ConflictPage.SelectedValueIndex;
    if ConflictChoice = ChoiceCancel then begin
      Result := False;
      MsgBox('Installation cancelled.', mbInformation, MB_OK);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    StopExistingDedicatedHost;
    SendShellRefresh;
    WizardForm.FinishedLabel.Caption :=
      'Installation complete. In File Explorer, press Alt+P to open the preview pane. ' +
      'This installer does not terminate or restart Explorer.';
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    RestoreRegistrations;
  end
  else if CurUninstallStep = usPostUninstall then
    SendShellRefresh;
end;
