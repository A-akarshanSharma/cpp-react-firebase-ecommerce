import { createContext, useCallback, useContext, useEffect, useRef, useState } from 'react'
import {
  onAuthStateChanged,
  signInWithEmailAndPassword,
  createUserWithEmailAndPassword,
  signOut,
} from 'firebase/auth'
import { auth, authConfigurationError } from '../services/firebase'
import { api } from '../services/api'
const AuthContext = createContext(null)
export function AuthProvider({ children }) {
  const [firebaseUser, setUser] = useState(null)
  const [profile, setProfile] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(authConfigurationError)
  const generation = useRef(0)
  const refreshProfile = useCallback(async (background = false) => {
    const version = ++generation.current
    const user = auth?.currentUser
    setUser(user || null)
    if (background !== true) {
      setProfile(null)
      setLoading(true)
    }
    setError(authConfigurationError)
    try {
      if (user) {
        const p = await api.getMe()
        if (version === generation.current) setProfile(p)
      }
    } catch (e) {
      if (version === generation.current) {
        setProfile(null)
        setError(e.message)
      }
    } finally {
      if (version === generation.current) setLoading(false)
    }
  }, [])
  useEffect(() => {
    if (!auth) {
      setLoading(false)
      return
    }
    const unsub = onAuthStateChanged(auth, refreshProfile)
    const invalid = () => {
      ++generation.current
      setProfile(null)
      setLoading(false)
      setError('Your session could not be verified. Sign out and sign in again.')
    }
    const denied = () => setProfile((p) => (p ? { ...p, isAdmin: false } : null))
    const focus = () => {
      if (auth.currentUser) refreshProfile(true)
    }
    window.addEventListener('session-invalid', invalid)
    window.addEventListener('access-denied', denied)
    window.addEventListener('focus', focus)
    return () => {
      ++generation.current
      unsub()
      window.removeEventListener('session-invalid', invalid)
      window.removeEventListener('access-denied', denied)
      window.removeEventListener('focus', focus)
    }
  }, [refreshProfile])
  const requireAuth = () => {
    if (!auth) throw new Error(authConfigurationError)
  }
  const login = (email, password) => {
    requireAuth()
    return signInWithEmailAndPassword(auth, email, password)
  }
  const signup = (email, password) => {
    requireAuth()
    return createUserWithEmailAndPassword(auth, email, password)
  }
  const logout = async () => {
    if (auth) await signOut(auth)
    ++generation.current
    setUser(null)
    setProfile(null)
    setError(authConfigurationError)
    setLoading(false)
  }
  const role = profile ? (profile.isAdmin ? 'admin' : 'customer') : null
  return (
    <AuthContext.Provider
      value={{
        firebaseUser,
        user: firebaseUser,
        profile,
        role,
        isAdmin: role === 'admin',
        authenticated: !!profile,
        loading,
        error,
        refreshProfile,
        login,
        signup,
        logout,
      }}
    >
      {children}
    </AuthContext.Provider>
  )
}
export const useAuth = () => useContext(AuthContext)
