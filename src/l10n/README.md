# Localization

https://www.transifex.com/conemu/conemu-sources/languages/


## Download translations (yaml)

```
:download_all
call :download de ConEmu_de.yaml
call :download es ConEmu_es.yaml
call :download ja ConEmu_ja.yaml
call :download ru ConEmu_ru.yaml
call :download zh ConEmu_zh.yaml
goto :EOF
:download
curl -k -L --user api:%TX_TOKEN% -X GET https://www.transifex.com/api/2/project/conemu-sources/resource/conemu-en-yaml--daily/translation/%1/?file=YAML_GENERIC -o %2
goto :EOF
```

## Upload english resources (yaml)

Refresh yaml files
```
Deploy\l10n_write_yaml.cmd
```

Upload `ConEmu_en.yaml` to transifex if it was not done automatically.

Use the button "Update source file" on the right-top of the page:
https://www.transifex.com/conemu/conemu-sources/conemu-en-yaml--daily/
