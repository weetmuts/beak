# Bash auto completion for beak

_beakusermounts()
{
    local cur=${COMP_WORDS[COMP_CWORD]}
    local uid=$(id -u)
    local mounts=$(cat /proc/mounts | grep fuse.beak | grep user_id=${uid} | cut -f 2 -d ' ' | sort)
    if [ "$mounts" != "" ]; then
        COMPREPLY=($(compgen -W "$mounts" -- $cur))
    fi
    return 0
}

_beakrules()
{
  COMPREPLY=""
  local cur=${COMP_WORDS[COMP_CWORD]}
  local rules=""
  if [ -f ~/.config/beak/beak.conf ]; then
      rules=$(grep -o \\[.*\\] ~/.config/beak/beak.conf | tr -d '[' | tr ']' ':' | sort)
      if [ "$rules" != "" ]; then
          COMPREPLY=($(compgen -W "$rules" -- $cur))
      fi
  fi
  return 0
}

_beakorigins()
{
  local cur=${COMP_WORDS[COMP_CWORD]}
  local rules=""
  if [ -f ~/.config/beak/beak.conf ]; then
      rules=$(grep -o \\[.*\\] ~/.config/beak/beak.conf | tr -d '[' | tr ']' ':' | sort)
  fi
  if [ "$rules" != "" ]; then
      COMPREPLY=($(compgen -W "$rules" -- $cur))
      if [ -z "$COMPREPLY" ]; then
          # No match for configured source trees. Try directories instead.
          _filedir -d
      fi
  else
      # No rules found, use directories.
      _filedir -d
  fi
  return 0
}

_beakstorages()
{
  local cur=${COMP_WORDS[COMP_CWORD]}
  local prev=${COMP_WORDS[COMP_CWORD-1]}
  if [ "$prev" = ":" ]; then prev=${COMP_WORDS[COMP_CWORD-2]}; fi
  local remotes="";
  if [ -f ~/.config/beak/beak.conf ]; then
      remotes=$(cat ~/.config/beak/beak.conf | grep -e 'remote\ *=' | sed 's/remote.*= \?//g' | sort | uniq)
  fi
  if [ "$remotes" != "" ]; then
      COMPREPLY=($(compgen -W "$remotes" -- $cur))
      if [ -z "$COMPREPLY" ]; then
          # No match for configured remotes trees. Try directories instead.
          _filedir -d
      fi
  else
      # No remotes found, use directories.
      _filedir -d
  fi
  return 0
}

_beak()
{
    local cur prev prevprev prevprevprev
    cur=${COMP_WORDS[COMP_CWORD]}
    prev=${COMP_WORDS[COMP_CWORD-1]}
    prevprev=${COMP_WORDS[COMP_CWORD-2]}
    prevprevprev=${COMP_WORDS[COMP_CWORD-3]}

    # The colon in configured rules gets its own CWORD....
    if [ "$prev" = ":" ]; then prev="$prevprev" ; prevprev="$prevprevprev"; fi

    case "$prev" in
        bmount) _beakorigins ;;
        diff) _beakorigins ;;
        fsck) _beakstorages ;;
        importmedia) _filedir -d ;;
        mount) _beakstorages ;;
        prune) _beakstorages ;;
        pull) _beakrules ;;
        push) _beakrules ;;
        pushd) _beakrules ;;
        restore) _beakstorages ;;
        shell) _beakstorages ;;
        status) _beakorigins ;;
        store) _beakstorages ;;
        stored) _beakstorages ;;
        umount) _beakusermounts ;;
    esac

    case "$prevprev" in
        bmount) _filedir -d ;;
        diff) _beakstorages ;;
        importmedia) _filedir -d ;;
        mount) _filedir -d ;;
        restore) _beakorigins ;;
        store) _beakstorages ;;
        stored) _beakstorages ;;
    esac

    if [ -z "$COMPREPLY" ]; then
        COMPREPLY=($(compgen -W "bmount config diff fsck genautocomplete genmounttrigger help prune importmedia mount pull push pushd restore shell status store stored umount" -- $cur))
    fi
}

complete -F _beak beak
