import { useState, useEffect } from 'react'
import { useParams, Link } from 'react-router-dom'

interface OrderDetail {
  id: number
  status: string
  quantity: number
  address: string
  note: string
  created_at: string
  updated_at: string
}

const statusSteps = [
  { key: 'pending', label: 'Order placed' },
  { key: 'confirmed', label: 'Confirmed' },
  { key: 'producing', label: 'In production' },
  { key: 'shipped', label: 'Shipped' },
]

const statusIndex: Record<string, number> = {
  pending: 0,
  confirmed: 1,
  producing: 2,
  shipped: 3,
}

export default function OrderDetailPage() {
  const { id } = useParams<{ id: string }>()
  const [order, setOrder] = useState<OrderDetail | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(false)

  useEffect(() => {
    fetch(`/api/orders/${id}/`)
      .then((res) => {
        if (!res.ok) throw new Error('Not found')
        return res.json()
      })
      .then(setOrder)
      .catch(() => setError(true))
      .finally(() => setLoading(false))
  }, [id])

  if (loading) {
    return <div className="mx-auto max-w-3xl px-6 py-16 text-neutral-500">Loading...</div>
  }

  if (error || !order) {
    return (
      <div className="mx-auto max-w-3xl px-6 py-16 text-center">
        <p className="text-lg text-neutral-900">Order not found</p>
        <Link to="/account" className="mt-2 inline-block text-sm text-neutral-900 underline">
          Back to account
        </Link>
      </div>
    )
  }

  const idx = statusIndex[order.status] ?? 0

  return (
    <div className="mx-auto max-w-3xl px-6 py-16 lg:px-12">
      <Link to="/account" className="mb-8 inline-block text-sm text-neutral-500 hover:text-neutral-900">
        &larr; Back to account
      </Link>

      <div className="mb-8">
        <h1 className="text-2xl font-semibold text-neutral-900">Order #{order.id}</h1>
        <p className="text-sm text-neutral-500">
          Placed on {new Date(order.created_at).toLocaleDateString()}
        </p>
      </div>

      {/* Status timeline */}
      <div className="mb-10">
        <div className="flex items-start">
          {statusSteps.map((step, i) => {
            const done = i <= idx && order.status !== 'cancelled'
            const cancelled = order.status === 'cancelled'
            return (
              <div key={step.key} className="flex-1">
                <div className="flex items-center">
                  <div
                    className={`h-4 w-4 rounded-full ${
                      cancelled && i === 0 ? 'bg-red-500' :
                      done ? 'bg-neutral-900' : 'bg-neutral-200'
                    }`}
                  />
                  {i < statusSteps.length - 1 && (
                    <div
                      className={`h-px flex-1 ${
                        done ? 'bg-neutral-900' : 'bg-neutral-200'
                      }`}
                    />
                  )}
                </div>
                <p className="mt-1 text-xs text-neutral-500">{step.label}</p>
              </div>
            )
          })}
        </div>
        {order.status === 'cancelled' && (
          <p className="mt-2 text-sm text-red-600">This order was cancelled.</p>
        )}
      </div>

      {/* Order details */}
      <div className="rounded-lg border border-neutral-200 p-6 space-y-4">
        <div>
          <p className="text-sm text-neutral-500">Quantity</p>
          <p className="text-neutral-900">{order.quantity}</p>
        </div>
        <div>
          <p className="text-sm text-neutral-500">Shipping address</p>
          <p className="text-neutral-900 whitespace-pre-wrap">{order.address}</p>
        </div>
        {order.note && (
          <div>
            <p className="text-sm text-neutral-500">Note</p>
            <p className="text-neutral-900 whitespace-pre-wrap">{order.note}</p>
          </div>
        )}
        <div>
          <p className="text-sm text-neutral-500">Last updated</p>
          <p className="text-neutral-900">{new Date(order.updated_at).toLocaleDateString()}</p>
        </div>
      </div>
    </div>
  )
}
