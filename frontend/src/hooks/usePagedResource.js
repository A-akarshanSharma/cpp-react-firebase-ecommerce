import { useCallback, useEffect, useRef, useState } from 'react'

// One request per explicit page action; refreshing starts a new list generation.
export function usePagedResource(loader) {
  const [state, setState] = useState({
    data: [],
    loading: true,
    error: '',
    moreError: '',
    loadingMore: false,
    nextCursor: null,
  })
  const generation = useRef(0)
  const pending = useRef(false)
  const cursor = useRef(null)
  const reload = useCallback(async () => {
    const version = ++generation.current
    pending.current = true
    cursor.current = null
    setState({
      data: [],
      loading: true,
      error: '',
      moreError: '',
      loadingMore: false,
      nextCursor: null,
    })
    try {
      const page = await loader()
      if (version !== generation.current) return
      cursor.current = page.nextCursor
      setState({
        data: page.items,
        loading: false,
        error: '',
        moreError: '',
        loadingMore: false,
        nextCursor: page.nextCursor,
      })
    } catch (e) {
      if (version === generation.current)
        setState((s) => ({ ...s, loading: false, error: e.message }))
    } finally {
      if (version === generation.current) pending.current = false
    }
  }, [loader])
  const loadMore = useCallback(async () => {
    if (pending.current || !cursor.current) return
    const version = generation.current,
      requested = cursor.current
    pending.current = true
    setState((s) => ({ ...s, loadingMore: true, moreError: '' }))
    try {
      const page = await loader(requested)
      if (version !== generation.current) return
      if (page.nextCursor === requested)
        throw new Error('The list could not advance. Refresh and try again.')
      cursor.current = page.nextCursor
      setState((s) => {
        const ids = new Set(s.data.map((item) => item.id))
        return {
          ...s,
          data: [...s.data, ...page.items.filter((item) => !ids.has(item.id))],
          nextCursor: page.nextCursor,
        }
      })
    } catch (e) {
      if (version === generation.current) setState((s) => ({ ...s, moreError: e.message }))
    } finally {
      if (version === generation.current) {
        pending.current = false
        setState((s) => ({ ...s, loadingMore: false }))
      }
    }
  }, [loader])
  useEffect(() => {
    reload()
    return () => {
      ++generation.current
    }
  }, [reload])
  return { ...state, reload, loadMore }
}
