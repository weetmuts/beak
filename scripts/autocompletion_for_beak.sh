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
  local cur=${COMP_WORDS[COMP_CWORD]}
  local rules=$(grep -o \\[.*\\] ~/.config/beak/beak.conf | tr -d '[' | tr ']' ':' | sort)
  if [ "$rules" != "" ]; then
      COMPREPLY=($(compgen -W "$rules" -- $cur))
      if [ -z "$COMPREPLY" ]; then
          # No match for configured source trees. Try directories instead.
          _filedir -d
      fi
  fi
  return 0
}

_beakremotes()
{
  local cur=${COMP_WORDS[COMP_CWORD]}
  local prev=${COMP_WORDS[COMP_CWORD-1]}
  if [ "$prev" = ":" ]; then prev=${COMP_WORDS[COMP_CWORD-2]}; fi
  local remotes=$(sed -n "/^\[${prev}\]/,/^\[/p" ~/.config/beak/beak.conf | grep -e 'remote\ *=' | sed 's/remote.*= \?//g' | sort)
  if [ "$remotes" != "" ]; then
      COMPREPLY=($(compgen -W "$remotes" -- $cur))
      if [ -z "$COMPREPLY" ]; then
          # No match for configured remotes trees. Try directories instead.
          _filedir -d
      fi
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
        mount) _beakrules ;;
        prune) _beakrules ;;
        pull) _beakrules ;;
        push) _beakrules ;;
        restore) _beakremotes ;;
        store) _beakrules ;;
        umount) _beakusermounts ;;
    esac

    case "$prevprev" in
        mount) _filedir -d ;;
        prune) _beakremotes ;;
        pull) _beakremotes ;;
        push) _beakremotes ;;
        restore) _beakrules ;;
        store) _beakremotes ;;
    esac

    if [ -z "$COMPREPLY" ]; then
        COMPREPLY=($(compgen -W "mount prune pull push restore store umount" -- $cur))
    fi
}

complete -F _beak beak
