import { useEffect, useState } from 'react'
import { useNavigate, useSearchParams } from 'react-router-dom'

export default function AuthCallback() {
  const navigate = useNavigate()
  const [searchParams] = useSearchParams()
  const [error, setError] = useState(false)

  useEffect(() => {
    const errorParam = searchParams.get('error')
    if (errorParam) {
      setError(true)
      return
    }

    fetch('/_allauth/browser/v1/auth/session')
      .then((res) => res.json())
      .then((data) => {
        if (data.data?.user) {
          navigate('/', { replace: true })
        } else {
          setError(true)
        }
      })
      .catch(() => setError(true))
  }, [navigate, searchParams])

  if (error) {
    return (
      <div className="flex min-h-[60vh] items-center justify-center">
        <div className="text-center">
          <p className="text-lg text-neutral-900">Sign in failed</p>
          <p className="mt-2 text-sm text-neutral-500">
            The sign-in process was cancelled or encountered an error.
          </p>
          <a href="/" className="mt-4 inline-block text-sm text-neutral-900 underline">
            Return home
          </a>
        </div>
      </div>
    )
  }

  return (
    <div className="flex min-h-[60vh] items-center justify-center">
      <p className="text-neutral-500">Signing you in...</p>
    </div>
  )
}
